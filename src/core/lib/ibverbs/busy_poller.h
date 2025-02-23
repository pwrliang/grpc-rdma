#ifndef GRPC_SRC_CORE_LIB_IBVERBS_BUSY_POLLER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_BUSY_POLLER_H
#ifdef GRPC_USE_IBVERBS

#include <array>

#include "src/core/config/config_vars.h"
#include "src/core/lib/ibverbs/pair.h"
#include "src/core/util/sync.h"

#define GRPC_IBVERBS_POLLER_CAPACITY (4096)
namespace grpc_event_engine::experimental {

class BusyPoller {
  BusyPoller() {
    running_ = true;
    tail_ = 0;
    std::fill(pairs_.begin(), pairs_.end(), 0);
    int n_pollers = grpc_core::ConfigVars::Get().RdmaPollerThreadNum();

    for (int i = 0; i < n_pollers; i++) {
      threads_.push_back(std::thread(&BusyPoller::begin_polling, this, i));
    }
    curr_ = 0;
  }

  ~BusyPoller() { Shutdown(); }

 public:
  static BusyPoller& Get() {
    static BusyPoller poller;
    return poller;
  }

  void Shutdown() {
    if (running_) {
      running_ = false;
      cv_.SignalAll();

      for (auto& th : threads_) {
        th.join();
      }
      LOG(INFO) << "Shutdown poller";
    }
  }

  void AddPollable(PairPollable* pollable);

  void RemovePollable(PairPollable* pollable);

 private:
  std::vector<std::thread> threads_;
  std::atomic_bool running_;
  std::atomic_uint32_t tail_;
  std::atomic_uint32_t curr_;
  std::atomic_uint32_t n_pairs_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;

  std::array<std::atomic_uint64_t, GRPC_IBVERBS_POLLER_CAPACITY> pairs_;

  void begin_polling(int poller_id);
};
}  // namespace grpc_event_engine::experimental
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_BUSY_POLLER_H
