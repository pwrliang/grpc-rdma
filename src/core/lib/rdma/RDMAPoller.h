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
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
#include "src/core/lib/rdma/cpu_stats.h"

extern bool rdmasr_is_server;

class RDMAPoller {
 public:
  ~RDMAPoller() {
    polling_ = false;
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
    if (!rdmasr_is_server) {
      return;
    }
    int reg_id = reg_id_++;
    int slot_id = reg_id % rdmasr_slots_.size();

    // ease the contentions
    pause_ = true;
    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    rdmasr->set_index(reg_id);
    rdmasr_slots_[slot_id].push_back(rdmasr);
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);

    gpr_mu_lock(&rdmasr_locks_[0]);
    while (monitor_ths_.size() < max_n_threads_) {
      monitor_ths_.emplace_back(
          [&, this](int thread_id) {
            size_t n_slots = rdmasr_slots_.size();
            if (rdmasr_is_server) {
              int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

              bind_thread_to_core(thread_id % num_cores);
            }

            pthread_setname_np(
                pthread_self(),
                ("RDMAPoller" + std::to_string(thread_id)).c_str());
            grpc_stats_time_init(200 + thread_id);

            while (polling_) {
              int n_threads = n_threads_;

              if (n_threads > 0) {
                size_t avg_n_slots = (n_slots + n_threads - 1) / n_threads;
                size_t begin_slot = std::min(n_slots, thread_id * avg_n_slots);
                size_t end_slot =
                    std::min(n_slots, (thread_id + 1) * avg_n_slots);

                for (int i = begin_slot; i < end_slot; i++) {
                  notifyWaiter(i);
                }
              }

              while (pause_) {
                std::this_thread::yield();
              }
            }
          },
          n_threads_++);
    }
    gpr_mu_unlock(&rdmasr_locks_[0]);
    pause_ = false;
  }

  void Unregister(RDMASenderReceiverBPEV* rdmasr) {
    if (!rdmasr_is_server) {
      return;
    }
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

  int max_n_threads() const { return max_n_threads_; }

 private:
  RDMAPoller() {
    max_n_threads_ = 1;
    // Only allowing server uses multiple pollers
    if (rdmasr_is_server) {
      char* s_nthreads = getenv("GRPC_RDMA_MAX_POLLER");

      if (s_nthreads != nullptr) {
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

        max_n_threads_ = atoi(s_nthreads);
        if (max_n_threads_ <= 0 || max_n_threads_ > num_cores) {
          gpr_log(GPR_ERROR, "Invalid env GRPC_RDMA_MAX_POLLER: %d",
                  max_n_threads_);
          abort();
        }
      }
    }

    int n_slots = max_n_threads_;

    n_threads_ = 0;
    monitor_ths_.reserve(1024);
    rdmasr_slots_.resize(n_slots);
    rdmasr_locks_.resize(n_slots);
    polling_ = true;
    pause_ = false;

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
    for (auto* rdmasr : rdmasr_slots_[slot_id]) {
      if (poll(rdmasr)) {
        do {
          sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
        } while (sz < 0 && errno == EAGAIN);
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
      double read_lag =
          (get_cycles() - rdmasr->get_last_recv_time()) / cpu_mhz_;
      // We found that messages will not be read within 5us, we notify again
      if (read_lag > 5) {
        readable = true;
      }
    }
    return readable;
  }

  int max_n_threads_;
  std::vector<gpr_mu> rdmasr_locks_;
  std::vector<std::vector<RDMASenderReceiverBPEV*>> rdmasr_slots_;
  std::atomic_int32_t reg_id_;
  std::atomic_uint32_t n_threads_;
  std::vector<std::thread> monitor_ths_;
  bool polling_;
  double cpu_mhz_;
  std::atomic_bool pause_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
