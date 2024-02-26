#ifdef GRPC_USE_IBVERBS
#include <thread>
#include <vector>

#include <grpc/support/log.h>

#include "src/core/lib/ibverbs/ring_buffer.h"

namespace grpc_core {
namespace ibverbs {

RingBufferPollable::RingBufferPollable()
    : buf_(nullptr),
      capacity_(0),
      capacity_mask_(0),
      head_(0),
      moving_head_(0),
      remain_(0) {}

RingBufferPollable::RingBufferPollable(uint8_t* buf, uint64_t size)
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
    gpr_log(GPR_ERROR, "retry size %lu head %lu remain %lu", size, head,
            remain);
    goto retry;
  }

  assert(size <= capacity_ - reserved_space);
  auto footer_offset = (head + alignment + round_up(size)) & capacity_mask_;
  auto* p_footer =
      reinterpret_cast<std::atomic_uint64_t*>(buf_ + footer_offset);

  if (p_footer->load() == footer) {
    return size;
  }
  return 0;
}

uint64_t RingBufferPollable::GetFreeSize(uint64_t head, uint64_t tail) const {
  assert(tail < capacity_mask_);
  assert(tail % alignment == 0);
  uint64_t size = (tail + capacity_ - head) & capacity_mask_;
  return capacity_ - size;
}

uint64_t RingBufferPollable::GetWritableSize(uint64_t head,
                                             uint64_t tail) const {
  uint64_t remaining = GetFreeSize(head, tail);

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
    if (internal_bytes_read != nullptr) {
      *internal_bytes_read = 0;
    }
    return 0;
  }

  if (remain_ == 0) {
    auto* p_header = reinterpret_cast<uint64_t*>(buf_ + head_);
    auto payload_size = *p_header;
    auto* p_footer = reinterpret_cast<uint64_t*>(
        buf_ + ((head_ + alignment + round_up(payload_size)) & capacity_mask_));

    moving_head_ = (head_ + alignment) & capacity_mask_;
    head_ =
        (head_ + 2ul * alignment + round_up(readable_size)) & capacity_mask_;
    assert(payload_size == readable_size);
    assert(*p_footer == footer);
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
    assert(*p_footer == footer);
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

uint64_t RingBufferPollable::Write(uint8_t* dst_buf, uint64_t tail,
                                   void* src_buf, uint64_t size) const {
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
  *reinterpret_cast<uint64_t*>(dst_buf + tail) = footer;
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

uint64_t RingBufferPollable::GetWriteRequests(
    uint64_t remote_tail, void* remote_addr, uint32_t rkey,
    std::vector<ibv_sge>& sg_list, std::array<ibv_send_wr, 2>& wrs) const {
  assert(remote_tail < capacity_);
  uint64_t next_tail = remote_tail;
  size_t circular_idx = sg_list.size();
  uint32_t seg1_size, seg2_size;
  uint32_t total_size = 0;

  for (size_t i = 0; i < sg_list.size(); i++) {
    auto& sge = sg_list[i];
    auto size = sge.length;
    next_tail = (next_tail + size) & capacity_mask_;

    GPR_ASSERT(size > 0);

    // Track the first circular case
    if (remote_tail > next_tail && circular_idx == sg_list.size()) {
      seg2_size = next_tail;
      seg1_size = size - seg2_size;

      if (seg1_size > 0 && seg2_size > 0) {
        circular_idx = i;
      }
    }
    total_size += size;
  }

  memset(wrs.data(), 0, sizeof(ibv_send_wr) * wrs.size());

  if (circular_idx < sg_list.size()) {  // circular case
    GPR_ASSERT(seg1_size > 0);
    GPR_ASSERT(seg2_size > 0);
    auto& sge1 = sg_list[circular_idx];
    sge1.length = seg1_size;

    ibv_sge sge2;
    sge2.addr = sge1.addr + sge1.length;
    sge2.length = seg2_size;
    sge2.lkey = sge1.lkey;

    sg_list.insert(sg_list.begin() + circular_idx + 1, sge2);

    uint32_t total_size1 = 0;
    for (int i = 0; i < sg_list.size() - 1; i++) {
      auto end = sg_list[i].addr + sg_list[i].length;
      auto next_begin = sg_list[i + 1].addr;
      if (end != next_begin) {
        gpr_log(
            GPR_ERROR,
            "Circular idx %zu, i %u, seg1 %u, seg2 %u, end %lu, next begin %lu",
            circular_idx, i, seg1_size, seg2_size, end, next_begin);
      }
      GPR_ASSERT(end == next_begin);
      total_size1 += sg_list[i].length;
    }
    total_size1 += sg_list.back().length;

    if (total_size != total_size1) {
      gpr_log(GPR_ERROR, "total %u, total1 %u", total_size, total_size1);
    }
    GPR_ASSERT(total_size == total_size1);

    wrs[0].sg_list = sg_list.data();
    wrs[0].num_sge = circular_idx + 1;
    wrs[0].opcode = IBV_WR_RDMA_WRITE;
    wrs[0].next = &wrs[1];
    wrs[0].send_flags = IBV_SEND_SIGNALED;
    wrs[0].wr.rdma.remote_addr =
        reinterpret_cast<uint64_t>(remote_addr) + remote_tail;
    wrs[0].wr.rdma.rkey = rkey;

    wrs[1].sg_list = sg_list.data() + circular_idx + 1;
    wrs[1].num_sge = sg_list.size() - wrs[0].num_sge;
    wrs[1].opcode = IBV_WR_RDMA_WRITE;
    wrs[1].next = nullptr;
    wrs[1].send_flags = IBV_SEND_SIGNALED;
    wrs[1].wr.rdma.remote_addr = reinterpret_cast<uint64_t>(remote_addr);
    wrs[1].wr.rdma.rkey = rkey;
  } else {
    wrs[0].sg_list = sg_list.data();
    wrs[0].num_sge = sg_list.size();
    wrs[0].opcode = IBV_WR_RDMA_WRITE;
    wrs[0].next = nullptr;
    wrs[0].send_flags = IBV_SEND_SIGNALED;
    wrs[0].wr.rdma.remote_addr =
        reinterpret_cast<uint64_t>(remote_addr) + remote_tail;
    wrs[0].wr.rdma.rkey = rkey;
  }

  return next_tail;
}

uint64_t RingBufferPollable::get_capacity() const { return capacity_; }

uint64_t RingBufferPollable::get_head() const { return moving_head_; }

}  // namespace ibverbs
}  // namespace grpc_core
#endif