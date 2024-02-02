#include "src/core/lib/ibverbs/poller.h"

#include <unistd.h>

#include <algorithm>
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
}

void Poller::RemovePollable(grpc_core::ibverbs::PairPollable* pollable) {
  uint32_t tail = tail_;
  for (int i = 0; i < tail; i++) {
    if (pairs_[i] == reinterpret_cast<uint64_t>(pollable)) {
      pairs_[i] = 0;
      break;
    }
  }
}

void Poller::poll() {
  while (running_) {
    uint32_t tail = tail_;

    for (uint32_t i = 0; i < tail; i++) {
      auto* pair = reinterpret_cast<PairPollable*>(pairs_[i].load());

      if (pair != nullptr) {
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
          auto err = grpc_wakeup_fd_wakeup(pair->get_wakeup_fd());

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