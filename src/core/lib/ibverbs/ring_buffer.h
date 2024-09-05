#ifndef GRPC_SRC_CORE_LIB_IBVERBS_RING_BUFFER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_RING_BUFFER_H
#ifdef GRPC_USE_IBVERBS
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

#include <grpc/slice.h>

#include "src/core/lib/ibverbs/buffer.h"

namespace grpc_core {
namespace ibverbs {

class PairPollable;

class RingBufferPollable {
 public:
  /**
   * Memory layout:
   * [head 8 bytes (size of payload)] [payload, aligned to 8 bytes] [footer 8
   * bytes (all bits are 1)]
   */
  using tag_t = uint64_t;
  static constexpr int alignment = sizeof(tag_t);
  static constexpr uint64_t footer = std::numeric_limits<tag_t>::max();
  // header,footer,an extra alignment to indicate the buffer is full
  static constexpr int reserved_space = 3ul * alignment;

  RingBufferPollable();

  RingBufferPollable(uint8_t* buf, uint64_t size);

  RingBufferPollable(const RingBufferPollable& other);

  RingBufferPollable& operator=(const RingBufferPollable& other);

  void Init();

  bool HasMessage() const;

  uint64_t GetReadableSize() const;

  uint64_t GetFreeSize(uint64_t head, uint64_t tail) const;

  uint64_t GetWritableSize(uint64_t head, uint64_t tail) const;

  uint64_t Read(void* dst_buf, uint64_t capacity,
                uint64_t* internal_bytes_read = nullptr);

  uint64_t Write(uint8_t* dst_buf, uint64_t tail, void* src_buf,
                 uint64_t size) const;

  uint64_t Write(uint64_t tail, void* src_buf, uint64_t size) const;

  static uint8_t* AppendHeader(uint8_t* p, uint64_t payload_size) {
    *reinterpret_cast<tag_t*>(p) = payload_size;
    return p + alignment;
  }

  static uint8_t* AppendPayload(uint8_t* p, void* src, uint64_t payload_size) {
    if (src != nullptr) {
      memcpy(p, src, payload_size);
    }
    return p + round_up(payload_size);
  }

  static uint8_t* AppendFooter(uint8_t* p) {
    *reinterpret_cast<tag_t*>(p) = footer;
    return p + alignment;
  }

  uint64_t NextTail(uint64_t tail, uint64_t size) {
    assert(size <= capacity_);
    return (tail + size) & capacity_mask_;
  }

  static uint64_t GetEncodedSize(uint64_t payload_size) {
    assert(payload_size > 0);
    return 2ul * alignment + round_up(payload_size);
  }

  static uint64_t CalculateWritableSize(uint64_t space) {
    int64_t free = space;
    free = std::max(0l, free - reserved_space);
    return round_down(free);
  }

  uint64_t GetWriteRequests(uint64_t remote_tail, void* remote_addr,
                            uint32_t rkey, std::vector<ibv_sge>& sg_list,
                            std::array<ibv_send_wr, 2>& wrs) const;

  uint64_t get_capacity() const;

  uint64_t get_head() const;

 private:
  uint8_t* buf_;
  uint64_t capacity_;
  uint64_t capacity_mask_;
  std::atomic_uint64_t head_;
  uint64_t moving_head_;
  std::atomic_uint64_t remain_;  // for partial read, use atomic for visibility

  friend class PairPollable;

  // RDMA
  friend class PairPollable;

  void check_empty(uint8_t* buf, uint64_t len) const {
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

  // Align given value up to given alignment
  static inline uint64_t round_down(uint64_t v) {
    if (v % alignment == 0) {
      return v;
    }
    return v - v % alignment;
  }
};
}  // namespace ibverbs

}  // namespace grpc_core
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_RING_BUFFER_H
