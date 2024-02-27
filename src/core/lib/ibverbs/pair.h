/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_PAIR_H
#define GRPC_SRC_CORE_LIB_IBVERBS_PAIR_H
#ifdef GRPC_USE_IBVERBS
#include <grpc/slice.h>
#include <unistd.h>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "src/core/lib/ibverbs/address.h"
#include "src/core/lib/ibverbs/buffer.h"
#include "src/core/lib/ibverbs/device.h"
#include "src/core/lib/ibverbs/memory_region.h"
#include "src/core/lib/ibverbs/ring_buffer.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

#define IBVERBS_PAIR_TAG_POLLABLE (0xa0)
#define IBVERBS_PAIR_TAG_EVENT (0xb0)

#define IBVERBS_CHECK(error, call) \
  ibverbsCheck(error, call, #call, __FILE__, __LINE__)
inline void ibverbsCheck(std::string& error, int no, const char* call,
                         const char* file, unsigned int line) {
  if (no != 0) {
    std::stringstream ss;
    ss << "call '" << call << "' failed: " << file << ':' << line << ")\n";
    error = ss.str();
    gpr_log(GPR_ERROR, "ibverbs error, %s, error code %d", error.c_str(), no);
  }
}

namespace grpc_core {
namespace ibverbs {

enum class PairStatus {
  kUninitialized,
  kInitialized,
  kConnected,
  kHalfClosed,
  kDisconnected,
  kError
};

struct rdma_write_request {
  uint64_t id;
  uint64_t addr;
  uint64_t lkey;
  uint64_t remote_addr;
  uint64_t rkey;
  uint64_t size;
  rdma_write_request()
      : id(0), addr(0), lkey(0), remote_addr(0), rkey(0), size(0) {}
};

#ifndef NDEBUG
class ContentAssertion {
 public:
  explicit ContentAssertion(std::atomic_int& counter) : counter_(counter) {
    GPR_ASSERT(counter_++ == 0);
  }

  ~ContentAssertion() { counter_--; }

 private:
  std::atomic_int& counter_;
};
#else
class ContentAssertion {
 public:
  explicit ContentAssertion(std::atomic_int&) {}
};
#endif
class PairPollable {
  static constexpr int kMaxBuffers = 8;
  static constexpr auto kRecvCompletionQueueCapacity = kMaxBuffers;
  static constexpr auto kSendCompletionQueueCapacity = kMaxBuffers;
  static constexpr auto kCompletionQueueCapacity =
      kRecvCompletionQueueCapacity + kSendCompletionQueueCapacity;
  static constexpr int WR_ID_MR = 100;
  static constexpr int WR_ID_DATA = 200;
  static constexpr int WR_ID_STATUS = 300;
  static constexpr int STATUS_CHECK_INTERVAL_MS = 500;

  enum BufferType {
    kDataBuffer = 0,
    kStatusBuffer,
    kZeroCopyBuffer,
    BufferNum
  };

  struct status_report {
    uint64_t remote_head;
    int peer_exit;
  };

 public:
  PairPollable();

  virtual ~PairPollable();

  PairPollable(const PairPollable& that) = delete;

  PairPollable& operator=(const PairPollable& that) = delete;

  /**
   * Init Pair
   */
  void Init();

  bool Connect(const std::vector<char>& bytes);

  uint64_t Send(void* buf, uint64_t size);

  uint64_t Send(iovec* iov, uint64_t iov_size);

  uint64_t Send(grpc_slice* slices, size_t slice_count, size_t byte_idx);

  uint64_t SendZerocopy(grpc_slice* slices, size_t slice_count,
                        size_t byte_idx);

  uint64_t Recv(void* buf, uint64_t capacity);

  bool HasMessage() const;

  bool HasPendingWrites() const;

  uint64_t GetReadableSize() const;

  uint64_t GetWritableSize() const;

  uint8_t* AllocateSendBuffer(size_t size);

  const Address& get_self_address() const { return self_; }

  const Address& get_peer_address() const { return peer_; }

  PairStatus get_status();

  void Disconnect();

  grpc_wakeup_fd* get_wakeup_fd();

  const std::string& get_error() const;

 private:
  // shared_ptr ensures Device destructed after pair
  std::shared_ptr<Device> dev_;

  Address self_;
  Address peer_;

  struct ibv_cq* cq_;
  struct ibv_qp* qp_;
  int max_sge_num_;

  PairStatus status_;
  std::string error_;
  std::atomic_uint64_t last_qp_query_ts_;

  uint64_t internal_read_size_;
  uint64_t remote_tail_;
  std::atomic_bool partial_write_;

  std::vector<ibv_sge> sg_list_;
  std::atomic_int pending_write_num_data_;
  std::atomic_int pending_write_num_status_;

  RingBufferPollable ring_buf_;
  std::atomic_uint32_t zerocopy_buffer_tail_;

  std::array<std::unique_ptr<MemoryRegion>, kMaxBuffers> mr_pending_send_;
  std::queue<std::unique_ptr<MemoryRegion>> mr_posted_recv_;
  std::array<std::unique_ptr<MemoryRegion>, kMaxBuffers> mr_peer_;

  std::array<std::unique_ptr<Buffer>, kMaxBuffers> send_buffers_;
  std::array<std::unique_ptr<Buffer>, kMaxBuffers> recv_buffers_;

  grpc_wakeup_fd wakeup_fd_;

  std::atomic_bool debugging_;
  std::atomic_int read_content_;
  std::atomic_int write_content_;
  std::atomic_uint64_t total_read_size_;
  std::atomic_uint64_t total_write_size_;
  std::thread monitor_thread_;
  std::atomic_uint64_t zerocopy_bytes_;
  std::atomic_uint64_t copy_bytes_;

  friend class RingBufferPollable;

  void initQPs();

  void closeQPs();

  void initSendBuffer(BufferType type, uint64_t size);

  void initRecvBuffer(BufferType type, uint64_t size);

  void sendMemoryRegion(int buffer_id, MemoryRegion* mr);

  void postReceiveMemoryRegion(MemoryRegion* mr);

  void syncMemoryRegion(int buffer_id);

  int pollCompletions();

  void waitDataWrites();

  void waitStatusWrites();

  void handleCompletion(ibv_wc* wc);

  void postWrite(const rdma_write_request& req);

  void postWrite(int wr_id, struct ibv_sge* sg_list, int num_seg,
                 uint64_t remote_addr, uint32_t rkey);

  void updateStatus();

  uint64_t get_remote_head() const {
    auto* status =
        reinterpret_cast<status_report*>(recv_buffers_[kStatusBuffer]->data());
    return status->remote_head;  // TODO atomic_load?
  }

  void printStatus() {
    auto* status_buf = recv_buffers_[kStatusBuffer].get();
    auto* status = reinterpret_cast<status_report*>(status_buf->data());
    char last_msg[1024];
    char thread_name[1024];

    std::sprintf(thread_name, "monitor_th_pair_%p", this);
    pthread_setname_np(pthread_self(), thread_name);

    while (debugging_) {
      char msg[1024];
      auto remote_head = status->remote_head;
      auto readable_size = GetReadableSize();
      auto pending_writes = HasPendingWrites();

      float ratio = (float)zerocopy_bytes_ / (zerocopy_bytes_ + copy_bytes_);

      sprintf(msg,
              "PID %d Read %lu Write %lu Readable %lu Pending Writes %d Head "
              "%lu Remote "
              "Head %lu "
              "Remote Tail %lu PendingComp Data %d PendingComp Status %d "
              "Status %d Zerocopy %.2f",
              getpid(), total_read_size_.load(), total_write_size_.load(),
              readable_size, pending_writes, ring_buf_.get_head(), remote_head,
              remote_tail_, pending_write_num_data_.load(),
              pending_write_num_status_.load(), get_status(), ratio);

      if (strcmp(last_msg, msg) != 0) {
        gpr_log(GPR_INFO, "Pair %p %s", this, msg);
        strcpy(last_msg, msg);
      }
      sleep(1);
    }
    gpr_log(GPR_INFO, "Monitor Exits, Pair %p", this);
  }
};

class PairPool {
  static constexpr int kInitPoolSize = 128;

  PairPool() {}

 public:
  PairPool(const PairPool&) = delete;

  PairPool& operator=(const PairPool&) = delete;

  static PairPool& Get() {
    static PairPool pool;
    return pool;
  }

  PairPollable* Take(const std::string& id) {
    std::unique_lock<std::shared_timed_mutex> lock(mu_);

    if (pairs_.empty()) {
      createPairs();
    }
    PairPollable* pair = pairs_.front();
    pairs_.pop();
    id_pair_[id] = pair;
    return pair;
  }

  void Putback(PairPollable* pair) {
    std::unique_lock<std::shared_timed_mutex> lock(mu_);

    auto it = pair_id_.find(pair);
    if (it != pair_id_.end()) {
      id_pair_.erase(it->second);
      pair_id_.erase(it);
    }

    pairs_.push(pair);
  }

  PairPollable* Get(const std::string& id) {
    std::shared_lock<std::shared_timed_mutex> lock(mu_);

    auto it = id_pair_.find(id);
    if (it != id_pair_.end()) {
      return it->second;
    }
    return nullptr;
  }

 private:
  std::shared_timed_mutex mu_;
  std::queue<PairPollable*> pairs_;
  std::unordered_map<std::string, PairPollable*> id_pair_;
  std::unordered_map<PairPollable*, std::string> pair_id_;

  void createPairs() {
    for (int i = 0; i < kInitPoolSize; i++) {
      pairs_.push(new PairPollable());
    }
  }
};

}  // namespace ibverbs
}  // namespace grpc_core
#endif
#endif  // GRPC_SRC_CORE_LIB_IBVERBS_PAIR_H
