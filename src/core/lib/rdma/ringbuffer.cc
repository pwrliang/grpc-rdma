#include "src/core/lib/rdma/ringbuffer.h"
#include "grpc/impl/codegen/log.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/debug/trace.h"
#define MIN3(a, b, c) MIN(a, MIN(b, c))
grpc_core::DebugOnlyTraceFlag grpc_trace_ringbuffer(false, "rdma_ringbuffer");

bool RingBufferEvent::Read(msghdr* msg, size_t& expected_read_size) {
  auto head = head_;
  GPR_ASSERT(expected_read_size > 0 && expected_read_size < capacity_);
  GPR_ASSERT(head < capacity_);

  size_t lens = 0, iov_rlen;
  uint8_t *iov_rbase, *rb_ptr;
  for (size_t i = 0, iov_offset = 0, n;
       lens < expected_read_size && i < msg->msg_iovlen; lens += n) {
    iov_rlen =
        msg->msg_iov[i].iov_len - iov_offset;  // rest space of current slice
    iov_rbase = static_cast<uint8_t*>(msg->msg_iov[i].iov_base) + iov_offset;
    rb_ptr = buf_ + head;
    n = MIN3(capacity_ - head, expected_read_size - lens, iov_rlen);
    memcpy(iov_rbase, rb_ptr, n);
#ifndef NDEBUG
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_ringbuffer)) {
      gpr_log(GPR_DEBUG, "read_to_msghdr, read %zu bytes from head %zu", n,
              head);
    }
#endif
    head += n;
    iov_offset += n;
    if (n == iov_rlen) {
      // all space of the current slice has been used up. move to next slice
      i++;
      iov_offset = 0;
    }
    head = head % capacity_;
  }

  updateHead(lens);
  expected_read_size = lens;

  garbage_ += lens;
  if (garbage_ >= capacity_ / 2) {
    garbage_ = 0;
    return true;
  }

  return false;
}
