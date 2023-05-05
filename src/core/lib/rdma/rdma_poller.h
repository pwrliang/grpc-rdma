#ifndef GRPC_CORE_LIB_RDMA_RDMA_POLLER_H
#define GRPC_CORE_LIB_RDMA_RDMA_POLLER_H
#include <sys/resource.h>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/support/log.h"
#include "grpcpp/get_clock.h"

#include "include/grpc/support/sync.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"
#define MAX_CONNECTIONS (4096)

class RDMAPoller {
 public:
  ~RDMAPoller() {
    polling_ = false;
    for (auto& th : monitor_ths_) {
      th.join();
    }
  }

  static RDMAPoller& GetInstance() {
    static RDMAPoller inst;
    return inst;
  }

  void Register(RDMASenderReceiverBPEV* rdmasr) {
    int reg_id = reg_id_++;

    rdmasr->set_index(reg_id);

    gpr_log(GPR_INFO,
            "Register rdmasr %d to RDMAPoller, curr size: %d, is_server: %d "
            "fd: %d",
            rdmasr->get_index(), tail_.load(), rdmasr->is_server(),
            rdmasr->get_wakeup_fd());
    int tail = tail_++;
    rdma_conns_[tail] = reinterpret_cast<int64_t>(rdmasr);
  }

  void Unregister(RDMASenderReceiverBPEV* rdmasr) {
    int tail = tail_;

    for (int i = 0; i < tail; i++) {
      if (reinterpret_cast<RDMASenderReceiverBPEV*>(rdma_conns_[i].load()) == rdmasr) {
        rdma_conns_[i] = 0;
        break;
      }
    }

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

    tail_ = 0;
    polling_ = true;

    while (monitor_ths_.size() < polling_thread_num_) {
      monitor_ths_.emplace_back(
          [&, this](int thread_id) {
            pthread_setname_np(
                pthread_self(),
                ("RDMAPoller" + std::to_string(thread_id)).c_str());
            produceEvents(thread_id);
          },
          n_threads_++);
    }
  }

  void produceEvents(int thread_id) {
    while (polling_) {
      if (compacting_) {
        continue;
      }

      int tail = tail_;
      bool need_compact = false;

      if (tail == 0) {
        std::this_thread::yield();
        continue;
      }

      for (int i = thread_id; i < tail; i += polling_thread_num_) {
        auto* rdmasr =
            reinterpret_cast<RDMASenderReceiverBPEV*>(rdma_conns_[i].load());

        if (rdmasr != nullptr) {
          if (rdmasr->ToEpollEvent() != 0) {
            ssize_t sz;
            uint64_t val = 1;

            do {
              sz = write(rdmasr->get_wakeup_fd(), &val, sizeof(val));
            } while (sz < 0 && errno == EAGAIN);
            //            printf("write\n");
          }
        } else {
          need_compact = true;
          break;
        }
      }

      if (need_compact && thread_id == 0) {
        compacting_ = true;
        size_t n_conns = 0;

        for (int i = 0; i < tail; i++) {
          if (rdma_conns_[i] != 0) {
            staging_[i] = rdma_conns_[i].load();
            n_conns++;
          }
        }

        for (int i = 0; i < n_conns; i++) {
          rdma_conns_[i] = staging_[i].load();
        }

        tail_ = n_conns;
        compacting_ = false;
      }
    }
  }

  int polling_thread_num_;

  std::array<std::atomic_int64_t, MAX_CONNECTIONS> rdma_conns_;
  std::array<std::atomic_int64_t, MAX_CONNECTIONS> staging_;
  std::atomic_int tail_;
  std::atomic_bool compacting_;

  std::atomic_int32_t reg_id_;
  std::atomic_uint32_t n_threads_;
  std::vector<std::thread> monitor_ths_;
  bool polling_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMA_POLLER_H
