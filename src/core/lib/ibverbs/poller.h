#ifndef GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#ifdef GRPC_USE_IBVERBS
#include <src/core/lib/gpr/env.h>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "src/core/lib/ibverbs/config.h"
#include "src/core/lib/ibverbs/pair.h"

#define GRPC_IBVERBS_POLLER_CAPACITY (4096)
namespace grpc_core {
namespace ibverbs {

class Poller {
  Poller() {
    running_ = true;
    tail_ = 0;
    std::fill(pairs_.begin(), pairs_.end(), 0);
    int n_pollers = Config::Get().get_poller_thread_num();

    for (int i = 0; i < n_pollers; i++) {
      threads_.push_back(std::thread(&Poller::begin_polling, this, i));
    }
    curr_ = 0;
  }

  ~Poller() { Shutdown(); }

 public:
  static Poller& Get() {
    static Poller poller;
    return poller;
  }

  void Shutdown() {
    if (running_) {
      running_ = false;
      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.notify_all();
      }

      for (auto& th : threads_) {
        th.join();
      }
      gpr_log(GPR_INFO, "Shutdown poller");
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
  std::condition_variable cv_;
  std::mutex mu_;

  std::array<std::atomic_uint64_t, GRPC_IBVERBS_POLLER_CAPACITY> pairs_;

  void begin_polling(int poller_id);
};
}  // namespace ibverbs
}  // namespace grpc_core
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
