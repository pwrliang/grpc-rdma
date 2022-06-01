#ifndef GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#define GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
#include <algorithm>
#include <unordered_map>
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/support/log.h"
#include "include/grpc/support/sync.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
int _bind_thread_to_core_(int core_id) {
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

extern bool rdmasr_is_server;
extern int mpirank, num_node;
class RDMAMonitor {
  using rdmasr_seq_t = std::vector<RDMASenderReceiverBPEV*>;

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
    int curr_rdmasr_id = registry_counter_++;
    int slot_id = curr_rdmasr_id % rdmasr_slots_.size();

    gpr_mu_lock(&rdmasr_locks_[slot_id]);
    rdmasr->set_index(curr_rdmasr_id);
    rdmasr_slots_[slot_id].push_back(rdmasr);
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);

    gpr_mu_lock(&rdmasr_locks_[0]);
    int n_threads = monitor_ths_.size();
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    int n_rdmasr = 0;
    for (auto& vec : rdmasr_slots_) {
      n_rdmasr += vec.size();
    }

    if (n_rdmasr == 1) {
      max_n_threads_ = 1;
    } else if (n_rdmasr <= 8) {
      max_n_threads_ = 2;
    } else if (n_rdmasr <= 16) {
      max_n_threads_ = 3;
    } else {
      max_n_threads_ = 5;
    }

    //    max_n_threads_ = std::min(num_cores, std::max(1, n_rdmasr / 4));

    if (n_threads < max_n_threads_) {
      monitor_ths_.emplace_back(
          [&, this](int thread_id) {
            auto n_slots = rdmasr_slots_.size();

            while (running_) {
              int slot_id = curr_slot_++ % rdmasr_slots_.size();

              for (int i = 0; i < n_slots; i++) {
                notifyWaiter(slot_id);
              }
            }
          },
          n_threads);
    }
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

 private:
  RDMAMonitor() : curr_slot_(0), registry_counter_(0) {
    int nslots = 8;
    rdmasr_slots_.resize(nslots);
    rdmasr_locks_.resize(nslots);
    running_ = true;

    for (int i = 0; i < nslots; i++) {
      rdmasr_slots_[i].reserve(4096);
      gpr_mu_init(&rdmasr_locks_[i]);

      monitor_ths_.emplace_back(
          [&, this](int slot_id) {
            int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
            if (rdmasr_is_server) {
              _bind_thread_to_core_(num_cores - slot_id - 1);
              printf("server: bind monitor %d to core %d\n", slot_id, num_cores - slot_id - 1);
            } else {
              int id_in_node = mpirank / num_node;
              _bind_thread_to_core_(num_cores - slot_id - id_in_node - 1);
              printf("client: mpirank= %ld, bind monitor %d to core %d\n", mpirank, slot_id, num_cores - slot_id - id_in_node - 1);
            }
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
          // printf("notify wakeup_fd %ld\n", rdmasr->get_wakeup_fd());
        } while (sz < 0 && errno == EAGAIN);
      }
    }
    gpr_mu_unlock(&rdmasr_locks_[slot_id]);
  }

  int max_n_threads_ = 1;
  std::vector<gpr_mu> rdmasr_locks_;
  std::vector<rdmasr_seq_t> rdmasr_slots_;
  std::atomic_uint32_t curr_slot_;
  std::atomic_int32_t registry_counter_;
  std::vector<std::thread> monitor_ths_;
  bool running_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMAMONITOR_H
