#include "src/core/lib/ibverbs/poller.h"

#include <unistd.h>

#include <algorithm>
namespace grpc_core {
namespace ibverbs {
void Poller::AddPollable(grpc_core::ibverbs::PairPollable* pollable) {
  std::lock_guard<std::mutex> lg(mu_);
  pairs_.push_back(pollable);
}

void Poller::RemovePollable(grpc_core::ibverbs::PairPollable* pollable) {
  std::lock_guard<std::mutex> lg(mu_);

  pairs_.erase(std::remove(pairs_.begin(), pairs_.end(), pollable),
               pairs_.end());
}

void Poller::poll() {
  std::vector<grpc_wakeup_fd*> fds;

  while (running_) {
    {
      std::lock_guard<std::mutex> lg(mu_);

      for (auto* pair : pairs_) {
        auto status = pair->get_status();
        bool trigger;

        switch (status) {
          case PairStatus::kConnected: {
            // N.B. Do not use GetWritableSize to trigger event, it slows down
            trigger = pair->GetReadableSize() || pair->GetRemainWriteSize();
            break;
          }
          case PairStatus::kHalfClosed:
          case PairStatus::kError: {
            trigger = true;
            break;
          }
          default:
            trigger = false;
        }

        if (trigger) {
          fds.push_back(pair->get_wakeup_fd());
        }
      }
    }

    for (auto* fd : fds) {
      auto err = grpc_wakeup_fd_wakeup(fd);

      if (err != GRPC_ERROR_NONE) {
        gpr_log(GPR_ERROR, "wakeup fd error");
      }
    }

    if (fds.empty()) {
      std::this_thread::yield();
    } else {
      fds.clear();
    }
  }
}

}  // namespace ibverbs
}  // namespace grpc_core