#ifndef GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#define GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#include <sys/resource.h>
#include <algorithm>
#include <unordered_map>
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/support/log.h"
#include "grpcpp/get_clock.h"
#include "include/grpc/support/sync.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
extern bool rdmasr_is_server;
class RDMAPoller {
 public:
  ~RDMAPoller() {
    running_ = false;
    for (auto& th : monitor_ths_) {
      th.join();
    }

    for (auto& mu : rdmasr_locks_) {
      gpr_mu_destroy(&mu);
    }
  }

  static RDMAPoller& GetInstance() {
    static RDMAPoller inst;
    return inst;
  }

  void Register(RDMASenderReceiverBPEV* rdmasr) {
    int reg_id = reg_id_++;
    int slot_id = reg_id % rdmasr_slots_.size();

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    rdmasr->set_index(reg_id);
    rdmasr_slots_[slot_id].push_back(rdmasr);
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);

    gpr_mu_lock(&rdmasr_locks_[0]);
    size_t n_rdmasr = 0;
    size_t max_thread = 1;

    for (auto& vec : rdmasr_slots_) {
      n_rdmasr += vec.size();
    }

    while (monitor_ths_.size() < max_thread) {
      monitor_ths_.emplace_back(
          [&, this](int thread_id) {
            size_t n_slots = rdmasr_slots_.size();
            if (rdmasr_is_server) {
              int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

              bind_thread_to_core(thread_id % num_cores);
            }

            grpc_stats_time_init(255);

            while (running_) {
              auto n_threads = n_threads_.load();
              size_t avg_n_slots = (n_slots + n_threads - 1) / n_threads;
              size_t begin_slot = std::min(n_slots, thread_id * avg_n_slots);
              size_t end_slot =
                  std::min(n_slots, (thread_id + 1) * avg_n_slots);

              for (int i = begin_slot; i < end_slot; i++) {
                notifyWaiter(i);
              }
            }
          },
          monitor_ths_.size());
    }
    n_threads_ = monitor_ths_.size();

    gpr_mu_unlock(&rdmasr_locks_[0]);
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

  size_t get_thread_num() const { return monitor_ths_.size(); }

 private:
  RDMAPoller() {
    int n_slots = 16;

    n_threads_ = 0;
    rdmasr_slots_.resize(n_slots);
    rdmasr_locks_.resize(n_slots);
    running_ = true;

    for (int i = 0; i < n_slots; i++) {
      rdmasr_slots_[i].reserve(4096);
      gpr_mu_init(&rdmasr_locks_[i]);
    }
    int no_cpu_freq_fail = 0;
    cpu_mhz_ = get_cpu_mhz(no_cpu_freq_fail);
  }

  void notifyWaiter(int slot_id) {
    ssize_t sz;
    uint64_t val = 1;

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    // TODO: We may design a more efficient scheduling (e.g. prioritized sched)
    for (auto* rdmasr : rdmasr_slots_[slot_id]) {
      if (poll(rdmasr)) {
        GRPCProfiler profiler(GRPC_STATS_TIME_RDMA_POLL);

        do {
          sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
        } while (sz < 0 && errno == EAGAIN);
        if (!rdmasr_is_server) {
          std::this_thread::yield();
        }
      }
    }

    gpr_mu_unlock(&rdmasr_locks_[slot_id]);
  }

  int bind_thread_to_core(int core_id) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores) {
      return EINVAL;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
  }

  bool poll(RDMASenderReceiverBPEV* rdmasr) {
    bool readable = false;

    if (rdmasr->get_unread_data_size() == 0 && rdmasr->check_incoming() > 0) {
      cycles_t last_recv_time = rdmasr->last_recv_time();
      double recv_lag_us = (get_cycles() - last_recv_time) / cpu_mhz_;
      bool first_set_event = !rdmasr->set_event();
      // notify again if working thread do not read within time limit
      if (first_set_event || recv_lag_us > 100) {
        readable = true;
      }
    }
    return readable;
  }

  std::vector<gpr_mu> rdmasr_locks_;
  std::vector<std::vector<RDMASenderReceiverBPEV*>> rdmasr_slots_;
  std::atomic_int32_t reg_id_;
  std::atomic_uint32_t n_threads_;
  std::vector<std::thread> monitor_ths_;
  bool running_;
  double cpu_mhz_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
