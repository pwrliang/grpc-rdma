
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#include <mutex>
#include <queue>
#include <unordered_set>

#include "src/core/lib/ibverbs/pair.h"
#define GRPC_IBVERBS_POLLER_CAPACITY (4096)
namespace grpc_core {
namespace ibverbs {

class Poller {
  Poller() {
    running_ = true;
    tail_ = 0;
    std::fill(pairs_.begin(), pairs_.end(), 0);
    threads_.push_back(std::thread(&Poller::poll, this));
  }

 public:
  static Poller& Get() {
    static Poller poller;
    return poller;
  }

  void Shutdown() {
    running_ = false;
    for (auto& th : threads_) {
      th.join();
    }
    gpr_log(GPR_INFO, "Shutdown poller");
  }

  void AddPollable(PairPollable* pollable);

  void RemovePollable(PairPollable* pollable);

 private:
  std::vector<std::thread> threads_;
  std::atomic_bool running_;
  std::atomic_uint32_t tail_;

  std::array<std::atomic_uint64_t, GRPC_IBVERBS_POLLER_CAPACITY> pairs_;
  std::mutex mu_;

  void poll();
};
}  // namespace ibverbs
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
