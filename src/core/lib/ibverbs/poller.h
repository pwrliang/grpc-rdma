
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#include <mutex>
#include <queue>
#include <unordered_set>

#include "src/core/lib/ibverbs/pair.h"
namespace grpc_core {
namespace ibverbs {

class Poller {
  Poller() {
    running_ = true;
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

  std::vector<PairPollable*> pairs_;
  std::mutex mu_;

  void poll();
};
}  // namespace ibverbs
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
