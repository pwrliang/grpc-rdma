#ifdef GRPC_USE_IBVERBS
#include <grpc/support/log.h>

#include "src/core/lib/ibverbs/buffer.h"

namespace grpc_core {
namespace ibverbs {
Buffer::Buffer(ibv_pd* pd, size_t size) : buffer_(size) {
  mr_ = ibv_reg_mr(pd, buffer_.data(), size,
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  GPR_ASSERT(mr_);
}

Buffer::~Buffer() { ibv_dereg_mr(mr_); }

size_t Buffer::size() const { return buffer_.size(); }

char* Buffer::data() { return buffer_.data(); }

const char* Buffer::data() const { return buffer_.data(); }

ibv_mr* Buffer::get_mr() { return mr_; }

}  // namespace ibverbs
}  // namespace grpc_core
#endif