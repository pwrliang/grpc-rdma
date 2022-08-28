#ifndef GRPC_CORE_LIB_RDMA_RDMA_POLLER_H
#define GRPC_CORE_LIB_RDMA_RDMA_POLLER_H
#include <sys/resource.h>
#include <algorithm>
#include <unordered_map>
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/support/log.h"
#include "grpcpp/get_clock.h"

#include "include/grpc/support/sync.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"

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
    int reg_id = reg_id_++;
    int slot_id = reg_id % rdmasr_slots_.size();

    // ease the contentions
    pause_ = true;
    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    rdmasr->set_index(reg_id);
    rdmasr_slots_[slot_id].push_back(rdmasr);
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);

    gpr_mu_lock(&rdmasr_locks_[0]);
    while (monitor_ths_.size() < polling_thread_num_) {
      monitor_ths_.emplace_back(
          [&, this](int thread_id) {
            size_t n_slots = rdmasr_slots_.size();
            int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

            bindThreadToCore(thread_id % num_cores);

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

    gpr_log(GPR_INFO,
            "Register rdmasr %d to RDMAPoller, curr size: %zu, is_server: %d "
            "fd: %d",
            rdmasr->get_index(), rdmasr_slots_[slot_id].size(),
            rdmasr->is_server(), rdmasr->get_wakeup_fd());
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

    gpr_log(GPR_INFO, "Unregister rdmasr %d to RDMAPoller is_server: %d fd: %d",
            rdmasr->get_index(), rdmasr->is_server(), rdmasr->get_wakeup_fd());
  }

  // get starting cpu
  int get_polling_thread_num() const {
    return n_threads_ > 0 ? polling_thread_num_ : 0;
  }

 private:
  RDMAPoller() {
    auto& config = RDMAConfig::GetInstance();

    polling_thread_num_ = config.get_polling_thread_num();

    gpr_log(GPR_INFO, "max_thread: %d", polling_thread_num_);

    int n_slots = polling_thread_num_;

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
  }

  void notifyWaiter(int slot_id) {
    ssize_t sz;
    uint64_t val = 1;

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    for (int i = 0; i < rdmasr_slots_[slot_id].size(); i++) {
      auto* rdmasr = rdmasr_slots_[slot_id][i];

      if (rdmasr->ToEpollEvent() != 0) {
        do {
          sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
        } while (sz < 0 && errno == EAGAIN);
      }
    }
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);
  }

  int bindThreadToCore(int core_id) {
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

  int polling_thread_num_;
  std::vector<gpr_mu> rdmasr_locks_;
  std::vector<std::vector<RDMASenderReceiverBPEV*>> rdmasr_slots_;
  std::atomic_int32_t reg_id_;
  std::atomic_uint32_t n_threads_;
  std::vector<std::thread> monitor_ths_;
  bool polling_;
  std::atomic_bool pause_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMA_POLLER_H
