
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_ADDRESS_H
#define GRPC_SRC_CORE_LIB_IBVERBS_ADDRESS_H
#include <infiniband/verbs.h>

#include <cstdint>
#include <string>
#include <vector>
namespace grpc_core {
namespace ibverbs {

class Address {
 public:
  Address();
  explicit Address(const std::vector<char>&);
  virtual ~Address() {}

  std::vector<char> bytes() const;
  std::string str() const;

 private:
  explicit Address(const Address&) = default;

  struct {
    uint32_t lid;
    uint32_t qpn;
    uint32_t psn;
    union ibv_gid ibv_gid;
    uint32_t tag;
    uint64_t ring_buffer_size;
  } addr_;

  // Pair can access addr_ directly
  friend class PairPollable;
};
}  // namespace ibverbs
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IBVERBS_ADDRESS_H
