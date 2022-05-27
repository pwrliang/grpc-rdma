#ifndef GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#define GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#include <algorithm>
#include <unordered_map>
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/support/log.h"
#include "include/grpc/support/sync.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
#define MAX_EVENTS 100

class RDMAMonitor {
 public:
  ~RDMAMonitor() { gpr_mu_destroy(&mu); }

  static RDMAMonitor& GetInstance() {
    static RDMAMonitor monitor;
    return monitor;
  }

  void Register(RDMASenderReceiverAdaptive* rdmasr) {
    gpr_mu_lock(&mu);
    GPR_ASSERT(std::find(rdmasr_vec.begin(), rdmasr_vec.end(), rdmasr) ==
               rdmasr_vec.end());

    rdmasr_vec.push_back(rdmasr);
    stats[rdmasr] = StatEntry();
    gpr_mu_unlock(&mu);

    epoll_event ev_fd;
    ev_fd.events = static_cast<uint32_t>(EPOLLOUT);
    ev_fd.data.ptr = reinterpret_cast<void*>(rdmasr);

    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, rdmasr->get_wakeup_fd(), &ev_fd) != 0) {
      switch (errno) {
        case EEXIST:
          break;
        default:
          gpr_log(GPR_ERROR, "epoll_ctl error, errno: %d, serrno: %s", errno,
                  strerror(errno));
          abort();
      }
    }
  }

  void Unregister(RDMASenderReceiverAdaptive* rdmasr) {
    gpr_mu_lock(&mu);
    auto it = std::find(rdmasr_vec.begin(), rdmasr_vec.end(), rdmasr);

    if (it != rdmasr_vec.end()) {
      rdmasr_vec.erase(it);

      epoll_event ev_fd;
      memset(&ev_fd, 0, sizeof(ev_fd));

      epoll_ctl(epfd_, EPOLL_CTL_ADD, rdmasr->get_wakeup_fd(), &ev_fd);
      stats.erase(rdmasr);
    }

    gpr_mu_unlock(&mu);
  }

  void Report(RDMASenderReceiverAdaptive* rdmasr) {
    gpr_mu_lock(&mu);
    StatEntry& entry = stats.at(rdmasr);
    gpr_mu_unlock(&mu);

    double period_ms = ToDoubleMilliseconds((absl::Now() - entry.last_time));

    entry.Inc();

    if (entry.count % 10 == 0) {
      if (!rdmasr->is_migrating()) {
        if (rdmasr->get_recv_mode() == RDMASenderReceiverMode::kBP) {
          rdmasr->switch_mode(RDMASenderReceiverMode::kEvent);
        } else {
          rdmasr->switch_mode(RDMASenderReceiverMode::kBP);
        }
      }
    }
    //    if (period_ms > 100) {
    //      double wait_time = entry.AvgWaitMicro(absl::Now());
    //
    //      gpr_log(GPR_INFO, "Wait time: %lf us", wait_time);
    //
    ////      if (!rdmasr->is_migrating()) {
    ////        if (wait_time > 500) {
    ////          rdmasr->switch_mode(RDMASenderReceiverMode::kEvent);
    ////        } else {
    ////          rdmasr->switch_mode(RDMASenderReceiverMode::kBP);
    ////        }
    ////      }
    //
    //      entry.Reset();
    //    }
  }

 private:
  RDMAMonitor() {
    gpr_mu_init(&mu);
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    monitor_th_ = std::thread([this]() {
      while (true) {
        notifyWaiter();

        //        gpr_mu_lock(&mu);
        //        if (!rdmasr_vec.empty()) {
        //          switchMode();
        //        } else {
        //          std::this_thread::yield();
        //        }
        //        gpr_mu_unlock(&mu);
      }
    });
    monitor_th_.detach();
  }

  void notifyWaiter() {
    uint64_t val = 1;
    ssize_t sz;
    int r;
    epoll_event events[MAX_EVENTS];

    do {
      r = epoll_wait(epfd_, events, MAX_EVENTS, 0);
    } while ((r < 0 && errno == EINTR));

    for (int i = 0; i < r; i++) {
      epoll_event& ev = events[i];

      if ((ev.events & EPOLLOUT) != 0) {
        auto* rdmasr =
            reinterpret_cast<RDMASenderReceiverAdaptive*>(ev.data.ptr);

        // Notify when
        // 1. BP
        // 2. Migrating from BP to Event
        // 2. Migrating from Event to BP
        if (rdmasr->get_recv_mode() == RDMASenderReceiverMode::kBP ||
            (rdmasr->is_migrating() &&
             rdmasr->get_recv_mode() == RDMASenderReceiverMode::kEvent)) {
          if (rdmasr->get_unread_data_size() == 0 &&
              rdmasr->check_incoming() > 0) {
            do {
              sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
            } while (sz < 0 && errno == EAGAIN);
          }
        }
      }
    }
  }

  void switchMode() {
    // TODO:
  }

  struct StatEntry {
    size_t count;
    absl::Time last_time;

    StatEntry() { Reset(); }

    void Reset() {
      count = 0;
      last_time = absl::Now();
    }

    void Inc() { count++; }

    double AvgWaitMicro(const absl::Time& now) const {
      return ToDoubleMicroseconds((now - last_time)) / count;
    }
  };

  std::vector<RDMASenderReceiverAdaptive*> rdmasr_vec;
  std::unordered_map<RDMASenderReceiverAdaptive*, StatEntry> stats;
  gpr_mu mu;
  std::thread monitor_th_;
  int epfd_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
