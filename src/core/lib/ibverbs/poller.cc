#ifdef GRPC_USE_IBVERBS

#include <poll.h>
#include <unistd.h>
#include <algorithm>

#include "absl/time/clock.h"

#include "src/core/lib/ibverbs/poller.h"
namespace grpc_core {
namespace ibverbs {
void Poller::AddPollable(grpc_core::ibverbs::PairPollable* pollable) {
  GPR_ASSERT(tail_ < GRPC_IBVERBS_POLLER_CAPACITY);
  bool inserted;
  do {
    uint32_t tail = tail_;
    int empty_slot = 0;

    for (; empty_slot < tail; empty_slot++) {
      if (pairs_[empty_slot] == 0) {
        break;
      }
    }

    if (empty_slot == tail) {
      pairs_[tail_++] = reinterpret_cast<uint64_t>(pollable);
      inserted = true;
    } else {
      uint64_t exp = 0;
      inserted = pairs_[empty_slot].compare_exchange_weak(
          exp, reinterpret_cast<uint64_t>(pollable));
    }
  } while (!inserted);
  n_pairs_++;
  {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.notify_one();
  }
}

void Poller::RemovePollable(grpc_core::ibverbs::PairPollable* pollable) {
  uint32_t tail = tail_;
  for (int i = 0; i < tail; i++) {
    if (pairs_[i] == reinterpret_cast<uint64_t>(pollable)) {
      pairs_[i] = 0;
      break;
    }
  }
  n_pairs_--;
}

void Poller::begin_polling(int poller_id) {
  absl::Time last_poll_time = absl::Now();
  auto poller_sleep_timeout = Config::Get().get_poller_sleep_timeout_ms();
  struct pollfd fds[1];
  gpr_log(GPR_INFO, "Poller started");

  while (running_) {
    if (n_pairs_ == 0) {
      if ((absl::Now() - last_poll_time) >
          absl::Milliseconds(poller_sleep_timeout)) {
        gpr_log(GPR_INFO, "Poller sleep");
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return n_pairs_ > 0; });
        gpr_log(GPR_INFO, "Poller wakeup");
      }
      continue;
    } else {
      last_poll_time = absl::Now();
    }

    uint32_t tail = tail_;
    uint32_t curr = curr_++;
    auto id = curr % tail;
    auto* pair = reinterpret_cast<PairPollable*>(pairs_[id].load());

    if (pair != nullptr) {
      auto wakeup_fd = pair->get_wakeup_fd();
      fds[0].fd = wakeup_fd->read_fd;
      fds[0].events = POLLIN;

      int rn = poll(fds, 1, 0);

      if (rn <= 0) {
        auto status = pair->get_status();
        bool trigger;

        switch (status) {
          case PairStatus::kConnected: {
            // N.B. Do not use GetWritableSize to trigger event, it slows down
            trigger = pair->HasMessage() || pair->HasPendingWrites();
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
          auto err = grpc_wakeup_fd_wakeup(wakeup_fd);

          if (err != GRPC_ERROR_NONE) {
            gpr_log(GPR_ERROR, "wakeup fd error, Pair %p", pair);
          }
        }
      }
    }
  }
}

}  // namespace ibverbs
}  // namespace grpc_core
#endif