#ifdef GRPC_USE_IBVERBS
#include "src/core/lib/ibverbs/address.h"

#include <array>

#include "absl/log/absl_check.h"

namespace grpc_core {
namespace ibverbs {

Address::Address() { memset(&addr_, 0, sizeof(addr_)); }

Address::Address(const std::vector<char>& bytes) {
  ABSL_CHECK_EQ(sizeof(addr_), bytes.size());
  memcpy(&addr_, bytes.data(), sizeof(addr_));
}

std::vector<char> Address::bytes() const {
  std::vector<char> bytes(sizeof(addr_));
  memcpy(bytes.data(), &addr_, sizeof(addr_));
  return bytes;
}

std::string Address::str() const {
  std::array<char, 128> buf;
  snprintf(buf.data(), buf.size(), "LID: %d QPN: %d PSN: %d", addr_.lid,
           addr_.qpn, addr_.psn);
  return std::string(buf.data());
}
}  // namespace ibverbs
}  // namespace grpc_core
#endif