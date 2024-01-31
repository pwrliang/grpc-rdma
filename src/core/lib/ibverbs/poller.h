
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#define GRPC_SRC_CORE_LIB_IBVERBS_POLLER_H
#include <mutex>
#include <queue>
#include <unordered_set>

#include "src/core/lib/ibverbs/pair.h"
namespace grpc_core {
namespace ibverbs {

class PairPool {
  static constexpr int kInitPoolSize = 100;

  PairPool() {
    struct grpc_core::ibverbs::attr attr;
    attr.port = 1;  // TODO: read from config
    attr.index = 0;
    dev_ = CreateDevice(attr);
  }

 public:
  PairPool(const PairPool&) = delete;

  PairPool& operator=(const PairPool&) = delete;

  static PairPool& Get() {
    static PairPool pool;
    return pool;
  }

  PairPollable* Take() {
    std::lock_guard<std::mutex> lg(mu_);

    if (pairs_.empty()) {
      createPairs();
    }
    PairPollable* pair = pairs_.front();
    pairs_.pop();
    return pair;
  }

  void Putback(PairPollable* pair) {
    std::lock_guard<std::mutex> lg(mu_);

    pairs_.push(pair);
  }

 private:
  std::mutex mu_;
  std::queue<PairPollable*> pairs_;
  std::shared_ptr<Device> dev_;

  void createPairs() {
    for (int i = 0; i < kInitPoolSize; i++) {
      pairs_.push(new PairPollable(dev_));
    }
  }
};

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
