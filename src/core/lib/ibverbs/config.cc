#include "src/core/lib/ibverbs/config.h"

namespace grpc_core {
namespace ibverbs {

Config::Config() { init(); }

Config& Config::Get() {
  static Config inst;
  return inst;
}

int Config::get_busy_polling_timeout_us() const {
  return busy_polling_timeout_us_;
}

int Config::get_poller_sleep_timeout_ms() const {
  return poller_sleep_timeout_ms_;
}

int Config::get_poller_thread_num() const { return poller_thread_num_; }

uint32_t Config::get_ring_buffer_size_kb() const {
  return ring_buffer_size_kb_;
}

bool Config::is_zero_copy() const { return zero_copy_; }

void Config::init() {
  char* s_val;

  s_val = gpr_getenv("GRPC_RDMA_POLLER_THREAD_NUM");

  if (s_val != nullptr) {
    poller_thread_num_ = atoi(s_val);
  } else {
    poller_thread_num_ = 1;
  }

  s_val = gpr_getenv("GRPC_RDMA_BUSY_POLLING_TIMEOUT_US");
  if (s_val != nullptr) {
    busy_polling_timeout_us_ = atoi(s_val);
  } else {
    busy_polling_timeout_us_ = 500;
  }

  s_val = gpr_getenv("GRPC_RDMA_POLLER_SLEEP_TIMEOUT_MS");
  if (s_val != nullptr) {
    poller_sleep_timeout_ms_ = atoi(s_val);
  } else {
    poller_sleep_timeout_ms_ = 1000;
  }

  s_val = gpr_getenv("GRPC_RDMA_RING_BUFFER_SIZE_KB");
  if (s_val != nullptr) {
    ring_buffer_size_kb_ = atoll(s_val);
  } else {
    ring_buffer_size_kb_ = 4 * 1024;
  }

  s_val = gpr_getenv("GRPC_RDMA_ZEROCOPY");
  zero_copy_ = s_val == nullptr || strcmp(s_val, "true") == 0;
}

}  // namespace ibverbs
}  // namespace grpc_core