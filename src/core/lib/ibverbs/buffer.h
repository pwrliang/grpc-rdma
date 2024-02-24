#ifndef GRPC_SRC_CORE_LIB_IBVERBS_BUFFER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_BUFFER_H
#ifdef GRPC_USE_IBVERBS
#include <infiniband/verbs.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace grpc_core {
namespace ibverbs {
class Buffer {
 public:
  Buffer(ibv_pd* pd, size_t size);

  ~Buffer();

  Buffer(const Buffer&) = delete;

  Buffer& operator=(const Buffer&) = delete;

  size_t size() const;

  char* data();

  const char* data() const;

  ibv_mr* get_mr();

 private:
  std::vector<char> buffer_;
  ibv_mr* mr_;
};
}  // namespace ibverbs
}  // namespace grpc_core
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_BUFFER_H
