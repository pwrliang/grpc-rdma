#ifndef GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#define GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#include <algorithm>
#include <unordered_map>
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/support/log.h"
#include "include/grpc/support/sync.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
extern bool mb_is_server;
class RDMAMonitor {
 public:
  ~RDMAMonitor() {
    running_ = false;
    for (auto& th : monitor_ths_) {
      th.join();
    }

    for (auto& mu : rdmasr_locks_) {
      gpr_mu_destroy(&mu);
    }
  }

  static RDMAMonitor& GetInstance() {
    static RDMAMonitor monitor;
    return monitor;
  }

  void Register(RDMASenderReceiverBPEV* rdmasr) {
    int reg_id = reg_id_++;
    int slot_id = reg_id % rdmasr_slots_.size();

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    rdmasr->set_index(reg_id);
    rdmasr_slots_[slot_id].push_back(rdmasr);
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);
  }

  void Unregister(RDMASenderReceiverBPEV* rdmasr) {
    int reg_id = rdmasr->get_index();
    int slot_id = reg_id % rdmasr_slots_.size();

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    auto& rdmasr_vec = rdmasr_slots_[slot_id];
    auto it = std::find(rdmasr_slots_[slot_id].begin(),
                        rdmasr_slots_[slot_id].end(), rdmasr);
    if (it != rdmasr_vec.end()) {
      rdmasr_vec.erase(it);
    }
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);
  }

 private:
  RDMAMonitor() {
    int n_monitor_threads;

    if (mb_is_server) {
      n_monitor_threads = 4;
    } else {
      n_monitor_threads = 1;
    }

    rdmasr_slots_.resize(n_monitor_threads);
    rdmasr_locks_.resize(n_monitor_threads);
    running_ = true;

    for (int i = 0; i < n_monitor_threads; i++) {
      rdmasr_slots_[i].reserve(4096);
      gpr_mu_init(&rdmasr_locks_[i]);

      monitor_ths_.emplace_back(
          [&, this](int slot_id) {
            while (running_) {
              notifyWaiter(slot_id);
            }
          },
          i);
    }
  }

  void notifyWaiter(int slot_id) {
    ssize_t sz;
    uint64_t val = 1;

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    // TODO: We may design a more efficient scheduling (e.g. prioritized sched)
    for (auto* rdmasr : rdmasr_slots_[slot_id]) {
      if (rdmasr->get_unread_data_size() == 0 && rdmasr->check_incoming() > 0) {
        do {
          sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
        } while (sz < 0 && errno == EAGAIN);
      }
    }

    gpr_mu_unlock(&rdmasr_locks_[slot_id]);
  }

  std::vector<gpr_mu> rdmasr_locks_;
  std::vector<std::vector<RDMASenderReceiverBPEV*>> rdmasr_slots_;
  std::atomic_int32_t reg_id_;
  std::vector<std::thread> monitor_ths_;
  bool running_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
