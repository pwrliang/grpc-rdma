#ifndef GRPC_SRC_CORE_LIB_IBVERBS_CONFIG_H
#define GRPC_SRC_CORE_LIB_IBVERBS_CONFIG_H
#ifdef GRPC_USE_IBVERBS
#include <cstdlib>
#include <cstring>
#include <string>

#include "src/core/lib/gpr/env.h"

namespace grpc_core {
namespace ibverbs {

class Config {
  Config();

 public:
  Config(const Config&) = delete;

  Config& operator=(const Config&) = delete;

  static Config& Get();

  const std::string& get_device_name() const;

  int get_port_num() const;

  int get_gid_index() const;

  int get_busy_polling_timeout_us() const;

  int get_poller_sleep_timeout_ms() const;

  int get_poller_thread_num() const;

  uint32_t get_ring_buffer_size_kb() const;

  bool is_zero_copy() const;

 private:
  void init();

  std::string device_name_;
  int port_num_;
  int gid_index_;

  int busy_polling_timeout_us_;
  int poller_sleep_timeout_ms_;
  int poller_thread_num_;
  uint32_t ring_buffer_size_kb_;
  bool zero_copy_;
};

}  // namespace ibverbs
}  // namespace grpc_core
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_CONFIG_H
