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
extern bool mb_is_server;
class RDMAMonitor {
 public:
  ~RDMAMonitor() { gpr_mu_destroy(&mu); }

  static RDMAMonitor& GetInstance() {
    static RDMAMonitor monitor;
    return monitor;
  }

  void Register(RDMASenderReceiverBPEV* rdmasr) {
    gpr_mu_lock(&mu);
    GPR_ASSERT(std::find(rdmasr_vec.begin(), rdmasr_vec.end(), rdmasr) ==
               rdmasr_vec.end());

    rdmasr_vec.push_back(rdmasr);
    int epfd = epfds_[reg_idx % n_monitors_];
    epfd_map[rdmasr] = epfd;
    reg_idx++;
    gpr_mu_unlock(&mu);

    epoll_event ev_fd;
    ev_fd.events = static_cast<uint32_t>(EPOLLOUT);
    ev_fd.data.ptr = reinterpret_cast<void*>(rdmasr);

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, rdmasr->get_wakeup_fd(), &ev_fd) != 0) {
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

  void Unregister(RDMASenderReceiverBPEV* rdmasr) {
    gpr_mu_lock(&mu);
    auto it = std::find(rdmasr_vec.begin(), rdmasr_vec.end(), rdmasr);

    if (it != rdmasr_vec.end()) {
      rdmasr_vec.erase(it);

      epoll_event ev_fd;
      memset(&ev_fd, 0, sizeof(ev_fd));
      int epfd = epfd_map[rdmasr];
      epoll_ctl(epfd, EPOLL_CTL_DEL, rdmasr->get_wakeup_fd(), &ev_fd);
    }

    gpr_mu_unlock(&mu);
  }

 private:
  RDMAMonitor() {
    gpr_mu_init(&mu);

    if (mb_is_server) {
      n_monitors_ = 4;
    } else {
      n_monitors_ = 1;
    }

    for (int i = 0; i < n_monitors_; i++) {
      epfds_.push_back(epoll_create1(EPOLL_CLOEXEC));
      monitor_ths_.emplace_back(
          [&, this](int monitor_idx) {
            while (true) {
              notifyWaiter(epfds_[monitor_idx]);
            }
          },
          i);
      monitor_ths_[i].detach();
    }
  }

  void notifyWaiter(int epfd) {
    uint64_t val = 1;
    ssize_t sz;
    int r;
    epoll_event events[MAX_EVENTS];

    do {
      r = epoll_wait(epfd, events, MAX_EVENTS, 0);
    } while ((r < 0 && errno == EINTR));

    for (int i = 0; i < r; i++) {
      epoll_event& ev = events[i];

      if ((ev.events & EPOLLOUT) != 0) {
        auto* rdmasr = reinterpret_cast<RDMASenderReceiverBPEV*>(ev.data.ptr);

        if (rdmasr->get_unread_data_size() == 0 &&
            rdmasr->check_incoming() > 0) {
          do {
            sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
          } while (sz < 0 && errno == EAGAIN);
        }
      }
    }
  }

  std::vector<RDMASenderReceiverBPEV*> rdmasr_vec;
  std::unordered_map<RDMASenderReceiverBPEV*, int> epfd_map;
  int reg_idx = 0;
  gpr_mu mu;
  int n_monitors_;
  std::vector<std::thread> monitor_ths_;
  std::vector<int> epfds_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
