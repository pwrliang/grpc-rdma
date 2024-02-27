#ifdef GRPC_USE_IBVERBS
#include <grpc/support/log.h>
#include <limits>

#include "src/core/lib/ibverbs/config.h"

namespace grpc_core {
namespace ibverbs {

Config::Config() { init(); }

Config& Config::Get() {
  static Config inst;
  return inst;
}

const std::string& Config::get_device_name() const { return device_name_; }

int Config::get_port_num() const { return port_num_; }

int Config::get_gid_index() const { return gid_index_; }

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

uint32_t Config::get_zerocopy_buffer_size_kb() const {
  return zerocopy_buffer_size_kb_;
}

uint32_t Config::get_zerocopy_threshold_kb() const {
  return zerocopy_threshold_kb_;
}

void Config::init() {
  char* s_val;

  s_val = gpr_getenv("GRPC_RDMA_DEVICE_NAME");
  if (s_val != nullptr) {
    device_name_ = std::string(s_val);
  }

  s_val = gpr_getenv("GRPC_RDMA_PORT_NUM");
  if (s_val != nullptr) {
    port_num_ = atoi(s_val);
  } else {
    port_num_ = 1;
  }

  s_val = gpr_getenv("GRPC_RDMA_GID_INDEX");
  if (s_val != nullptr) {
    gid_index_ = atoi(s_val);
  } else {
    gid_index_ = 0;
  }

  s_val = gpr_getenv("GRPC_RDMA_POLLER_THREAD_NUM");

  if (s_val != nullptr) {
    poller_thread_num_ = atoi(s_val);
    GPR_ASSERT(poller_thread_num_ > 0);
  } else {
    poller_thread_num_ = 1;
  }

  s_val = gpr_getenv("GRPC_RDMA_BUSY_POLLING_TIMEOUT_US");
  if (s_val != nullptr) {
    busy_polling_timeout_us_ = atoi(s_val);
    GPR_ASSERT(busy_polling_timeout_us_ >= 0);
  } else {
    busy_polling_timeout_us_ = 500;
  }

  s_val = gpr_getenv("GRPC_RDMA_POLLER_SLEEP_TIMEOUT_MS");
  if (s_val != nullptr) {
    poller_sleep_timeout_ms_ = atoi(s_val);
    GPR_ASSERT(poller_sleep_timeout_ms_ >= 0);
  } else {
    poller_sleep_timeout_ms_ = 1000;
  }

  s_val = gpr_getenv("GRPC_RDMA_RING_BUFFER_SIZE_KB");
  if (s_val != nullptr) {
    ring_buffer_size_kb_ = atoll(s_val);
    GPR_ASSERT(ring_buffer_size_kb_ > 0);
  } else {
    ring_buffer_size_kb_ = 4 * 1024;
  }

  s_val = gpr_getenv("GRPC_RDMA_RING_BUFFER_SIZE_KB");
  if (s_val != nullptr) {
    zerocopy_buffer_size_kb_ = atoll(s_val);
    GPR_ASSERT(zerocopy_buffer_size_kb_ > 0);
  } else {
    zerocopy_buffer_size_kb_ = 4 * 1024;
  }

  s_val = gpr_getenv("GRPC_RDMA_ZEROCOPY_THRESHOLD_KB");
  if (s_val != nullptr) {
    zerocopy_threshold_kb_ = atoll(s_val);
  } else {
    // disable by default
    zerocopy_threshold_kb_ = std::numeric_limits<uint32_t>::max();
  }
}

}  // namespace ibverbs
}  // namespace grpc_core
#endif