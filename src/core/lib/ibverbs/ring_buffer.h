
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_RING_BUFFER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_RING_BUFFER_H
#include <infiniband/verbs.h>

#include <sys/uio.h>

#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "src/core/lib/ibverbs/buffer.h"

namespace grpc_core {
namespace ibverbs {

class PairPollable;

struct ring_buffer_write_request {
  uint64_t src_offset;
  uint64_t dst_offset;
  uint64_t size;

  ring_buffer_write_request() : ring_buffer_write_request(0, 0, 0) {}

  ring_buffer_write_request(uint64_t s_off, uint64_t d_off, uint64_t sz)
      : src_offset(s_off), dst_offset(d_off), size(sz) {}
};

class RingBuffer {};

class RingBufferPollable {
  /**
   * Memory layout:
   * [head 8 bytes (size of payload)] [payload, aligned to 8 bytes] [footer 8
   * bytes (all bits are 1)]
   */
  static constexpr int alignment = sizeof(uint64_t);
  static constexpr uint64_t footer_tag = std::numeric_limits<uint64_t>::max();
  // header,footer,an extra alignment to indicate the buffer is full

 public:
  static constexpr uint64_t reserved_space = 3ul * alignment;

  RingBufferPollable();

  RingBufferPollable(char* buf, uint64_t size);

  RingBufferPollable(const RingBufferPollable& other);

  RingBufferPollable& operator=(const RingBufferPollable& other);

  void Init();

  static uint64_t CalculateCapacity(uint64_t max_payload_size) {
    return next_power_2(max_payload_size + reserved_space);
  }

  bool IsReadable() const { return GetReadableSize() > 0; }

  uint64_t GetReadableSize() const;

  uint64_t GetWritableSize(uint64_t head, uint64_t tail) const;

  uint64_t GetWritableSize(uint64_t tail) const;

  uint64_t Read(void* dst_buf, uint64_t capacity,
                uint64_t* internal_bytes_read = nullptr);

  uint64_t Write(char* dst_buf, uint64_t tail, void* src_buf,
                 uint64_t size) const;

  uint64_t Write(uint64_t tail, void* src_buf, uint64_t size) const;

  static uint64_t EncodeBuffer(char* buf, void* src_buf, uint64_t size) {
    *reinterpret_cast<uint64_t*>(buf) = size;
    memcpy(buf + alignment, src_buf, size);
    *reinterpret_cast<uint64_t*>(buf + alignment + round_up(size)) = footer_tag;
    return 2ul * alignment + round_up(size);
  }

  static uint64_t EncodeBuffer(char* buf, uint64_t payload_size_limit,
                               iovec* iov, uint64_t iov_size,
                               uint64_t& encoded_payload_size) {
    uint64_t offset = alignment;
    encoded_payload_size = 0;

    for (int i = 0; i < iov_size; i++) {
      uint64_t remain = 0;

      if (payload_size_limit > offset - alignment) {
        remain = payload_size_limit - (offset - alignment);
      }

      if (remain < alignment) {  // reserve an alignment as we need to roundup
        break;
      }

      auto len = std::min(iov[i].iov_len, remain);
      memcpy(buf + offset, iov[i].iov_base, len);
      offset += len;
      encoded_payload_size += len;
    }

    if (encoded_payload_size > 0) {
      *reinterpret_cast<uint64_t*>(buf) = offset - alignment;
      *reinterpret_cast<uint64_t*>(buf + round_up(offset)) = footer_tag;
      return alignment + round_up(offset);
    }
    return 0;
  }

  uint64_t GetWriteRequests(uint64_t size, uint64_t tail,
                            std::vector<ring_buffer_write_request>& reqs);

  char* get_buf();

  uint64_t get_capacity() const;

  uint64_t get_head() const;

 private:
  char* buf_;
  uint64_t capacity_;
  uint64_t capacity_mask_;
  std::atomic_uint64_t head_;
  uint64_t moving_head_;
  std::atomic_uint64_t remain_;  // for partial read, use atomic for visibility

  friend class PairPollable;

  // RDMA
  friend class PairPollable;

  void check_empty(char* buf, uint64_t len) const {
    for (uint64_t i = 0; i < len; i++) {
      assert(buf[i] == 0);
    }
  }

  // Adapted from
  // http://stackoverflow.com/questions/466204/rounding-up-to-nearest-power-of-2
  static inline uint64_t next_power_2(uint64_t v) {
    --v;
    for (int i = 1; i < sizeof(v) * CHAR_BIT; i *= 2) {
      v |= v >> i;
    }
    return ++v;
  }

  // Align given value up to given alignment
  static inline uint64_t round_up(uint64_t v) {
    if (v % alignment == 0) {
      return v;
    }
    return v - v % alignment + alignment;
  }
};
}  // namespace ibverbs

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IBVERBS_RING_BUFFER_H
