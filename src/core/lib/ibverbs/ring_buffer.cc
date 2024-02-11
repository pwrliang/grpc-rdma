#include "src/core/lib/ibverbs/ring_buffer.h"

#include <thread>

#include <grpc/support/log.h>

namespace grpc_core {
namespace ibverbs {

RingBufferPollable::RingBufferPollable()
    : buf_(nullptr),
      capacity_(0),
      capacity_mask_(0),
      head_(0),
      moving_head_(0),
      remain_(0) {}

RingBufferPollable::RingBufferPollable(char* buf, uint64_t size)
    : buf_(buf), capacity_(size), head_(0), moving_head_(0), remain_(0) {
  assert((capacity_ & (capacity_ - 1)) == 0);
  assert(capacity_ > reserved_space);
  capacity_mask_ = capacity_ - 1;
}

RingBufferPollable::RingBufferPollable(const RingBufferPollable& other)
    : buf_(other.buf_),
      capacity_(other.capacity_),
      capacity_mask_(other.capacity_mask_),
      head_(other.head_.load()),
      moving_head_(other.moving_head_),
      remain_(other.remain_.load()) {}

RingBufferPollable& RingBufferPollable::operator=(
    const RingBufferPollable& other) {
  if (&other == this) {
    return *this;
  }
  buf_ = other.buf_;
  capacity_ = other.capacity_;
  capacity_mask_ = other.capacity_mask_;
  head_ = other.head_.load();
  moving_head_ = other.moving_head_;
  remain_ = other.remain_.load();
  return *this;
}

void RingBufferPollable::Init() {
  memset(buf_, 0, capacity_);
  head_ = 0;
  moving_head_ = 0;
  remain_ = 0;
}

bool RingBufferPollable::HasMessage() const {
  uint64_t remain = remain_;
  uint64_t head = head_;
  if (remain > 0) {
    return remain;
  }
  assert(head % alignment == 0);
  auto* p_header = reinterpret_cast<std::atomic_uint64_t*>(buf_ + head);
  return p_header->load() > 0;
}

uint64_t RingBufferPollable::GetReadableSize() const {
retry:
  uint64_t remain = remain_;
  uint64_t head = head_;
  if (remain > 0) {
    return remain;
  }
  assert(head % alignment == 0);
  auto* p_header = reinterpret_cast<std::atomic_uint64_t*>(buf_ + head);
  auto size = p_header->load();

  if (size == 0) {
    return 0;
  }

  if (size > capacity_ - reserved_space) {
    gpr_log(GPR_ERROR, "retry size %lu head %lu remain %lu\n", size, head,
            remain);
    goto retry;
  }

  assert(size <= capacity_ - reserved_space);
  auto footer_offset = (head + alignment + round_up(size)) & capacity_mask_;
  auto* p_footer =
      reinterpret_cast<std::atomic_uint64_t*>(buf_ + footer_offset);

  if (p_footer->load() == footer_tag) {
    return size;
  }
  return 0;
}

uint64_t RingBufferPollable::GetWritableSize(uint64_t head,
                                             uint64_t tail) const {
  assert(tail < capacity_mask_);
  assert(tail % alignment == 0);
  uint64_t size = (tail + capacity_ - head) & capacity_mask_;
  uint64_t remaining = capacity_ - size;

  if (remaining > reserved_space) {
    remaining -= reserved_space;
  } else {
    remaining = 0;
  }
  return remaining;
}

uint64_t RingBufferPollable::GetWritableSize(uint64_t tail) const {
  return GetWritableSize(moving_head_, tail);
}

uint64_t RingBufferPollable::Read(void* dst_buf, uint64_t capacity,
                                  uint64_t* internal_bytes_read) {
  auto readable_size = GetReadableSize();
  auto copy_size = std::min(readable_size, capacity);
  uint64_t prev_moving_head = moving_head_;

  if (copy_size == 0) {
    return 0;
  }

  if (remain_ == 0) {
    auto* p_header = reinterpret_cast<uint64_t*>(buf_ + head_);

    moving_head_ = (head_ + alignment) & capacity_mask_;
    head_ = (head_ + 2 * alignment + round_up(readable_size)) & capacity_mask_;
    assert(*p_header == readable_size);
    *p_header = 0;  // clear header
  }

  uint64_t payload_end = (moving_head_ + copy_size) & capacity_mask_;
  uint64_t size_seg1, size_seg2 = 0;

  if (moving_head_ < payload_end) {
    size_seg1 = copy_size;
  } else {  // circular case
    size_seg2 = payload_end;
    size_seg1 = copy_size - size_seg2;
  }

  memcpy(reinterpret_cast<char*>(dst_buf), buf_ + moving_head_, size_seg1);
  memset(buf_ + moving_head_, 0, size_seg1);  // clear payload

  if (size_seg2 > 0) {
    memcpy(reinterpret_cast<char*>(dst_buf) + size_seg1, buf_, size_seg2);
    memset(buf_, 0, size_seg2);
  }

  moving_head_ = (moving_head_ + copy_size) & capacity_mask_;
  remain_ = readable_size - copy_size;

  // payload has been read
  if (remain_ == 0) {
    // clear padded space
    for (auto pos = moving_head_; pos < round_up(moving_head_); pos++) {
      buf_[pos & capacity_mask_] = 0;
    }
    // skip padded spce
    moving_head_ = round_up(moving_head_) & capacity_mask_;
    auto* p_footer = reinterpret_cast<uint64_t*>(buf_ + moving_head_);
    assert(*p_footer == footer_tag);
    *p_footer = 0;  // clear footer
    moving_head_ = (moving_head_ + alignment) & capacity_mask_;
    assert(moving_head_ == head_);
  }

  if (internal_bytes_read != nullptr) {
    *internal_bytes_read =
        (moving_head_ + capacity_ - prev_moving_head) & capacity_mask_;
  }

  return copy_size;
}

uint64_t RingBufferPollable::Write(char* dst_buf, uint64_t tail, void* src_buf,
                                   uint64_t size) const {
  assert(tail < capacity_);
  if (size == 0) {
    return tail;
  }
  assert(*reinterpret_cast<uint64_t*>(dst_buf + tail) == 0);
  *reinterpret_cast<uint64_t*>(dst_buf + tail) = size;
  tail = (tail + alignment) & capacity_mask_;

  uint64_t end = (tail + size) & capacity_mask_;
  uint64_t size_seg1;

  if (tail < end) {
    size_seg1 = size;
  } else {  // circular case
    size_seg1 = size - end;
  }

  check_empty(dst_buf + tail, size_seg1);
  memcpy(dst_buf + tail, src_buf, size_seg1);

  uint64_t size_seg2 = size - size_seg1;

  if (size_seg2 > 0) {
    check_empty(dst_buf, size_seg2);
    memcpy(dst_buf, static_cast<char*>(src_buf) + size_seg1, size_seg2);
  }
  tail = (tail + round_up(size)) & capacity_mask_;
  assert(*reinterpret_cast<uint64_t*>(dst_buf + tail) == 0);
  *reinterpret_cast<uint64_t*>(dst_buf + tail) = footer_tag;
  tail = (tail + alignment) & capacity_mask_;
  return tail;
}

uint64_t RingBufferPollable::Write(uint64_t tail, void* src_buf,
                                   uint64_t size) const {
  return Write(buf_, tail, src_buf, size);
}

uint64_t RingBufferPollable::GetWriteRequests(
    uint64_t size, uint64_t tail,
    std::vector<ring_buffer_write_request>& reqs) {
  reqs.clear();
  assert(tail < capacity_);
  if (size == 0) {
    return tail;
  }
  uint64_t end = (tail + size) & capacity_mask_;
  uint64_t size_seg1;

  if (tail < end) {
    size_seg1 = size;
  } else {  // circular case
    size_seg1 = size - end;
  }

  reqs.emplace_back(0, tail, size_seg1);

  uint64_t size_seg2 = size - size_seg1;

  if (size_seg2 > 0) {
    reqs.emplace_back(size_seg1, 0, size_seg2);
  }
  tail = (tail + size) & capacity_mask_;
  return tail;
}

char* RingBufferPollable::get_buf() { return buf_; }

uint64_t RingBufferPollable::get_capacity() const { return capacity_; }

uint64_t RingBufferPollable::get_head() const { return moving_head_; }

}  // namespace ibverbs
}  // namespace grpc_core