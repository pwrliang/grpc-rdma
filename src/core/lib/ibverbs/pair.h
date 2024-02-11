/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef GRPC_SRC_CORE_LIB_IBVERBS_PAIR_H
#define GRPC_SRC_CORE_LIB_IBVERBS_PAIR_H
#include <unistd.h>
#include <mutex>
#include <queue>
#include <sstream>
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
    ss << "Optix call '" << call << "' failed: " << file << ':' << line
       << ")\n";
    error = ss.str();
    gpr_log(GPR_INFO, "ibverbs error, %s", error.c_str());
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
  static constexpr int kSendBufSize = 2 * 1024 * 1024;
  static constexpr int kRecvBufSize = 4 * 1024 * 1024;
  //    static constexpr int kSendBufSize = 64;
  //    static constexpr int kRecvBufSize = 128;
  static constexpr int WR_ID_MR = 100;
  static constexpr int WR_ID_DATA = 200;
  static constexpr int WR_ID_STATUS = 300;
  static constexpr int STATUS_CHECK_INTERVAL_MS = 500;

  enum BufferType { kDataBuffer = 0, kStatusBuffer, BufferNum };

  struct status_report {
    uint64_t remote_head;
    int peer_exit;
  };

 public:
  explicit PairPollable(const std::shared_ptr<Device>& dev);

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

  uint64_t Recv(void* buf, uint64_t capacity);

  bool HasMessage() const;

  uint64_t GetReadableSize() const;

  uint64_t GetWritableSize() const;

  uint64_t GetRemainWriteSize() const;

  const Address& get_self_address() const { return self_; }

  const Address& get_peer_address() const { return peer_; }

  PairStatus get_status();

  void Disconnect();

  grpc_wakeup_fd* get_wakeup_fd();

  const std::string& get_error() const;

 private:
  std::shared_ptr<Device> dev_;

  Address self_;
  Address peer_;

  struct ibv_cq* cq_;
  struct ibv_qp* qp_;

  PairStatus status_;
  std::string error_;
  std::atomic_uint64_t last_qp_query_ts_;

  uint64_t internal_read_size_;
  uint64_t remote_tail_;
  std::atomic_uint64_t remain_write_size_;

  std::atomic_int pending_write_num_data_;
  std::atomic_int pending_write_num_status_;

  RingBufferPollable ring_buf_;

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

  void updateStatus();

  void printStatus() {
    auto* status_buf = recv_buffers_[kStatusBuffer].get();
    auto* status = reinterpret_cast<status_report*>(status_buf->data());
    char last_msg[1024];

    while (debugging_) {
      char msg[1024];
      auto remote_head = status->remote_head;
      auto readable_size = GetReadableSize();

      sprintf(msg,
              "PID %d Read %lu Write %lu Readable Size %lu Head %lu Remote "
              "Head %lu "
              "Remote Tail %lu PendingWrite Data %d PendingWrite Status %d "
              "Status %d",
              getpid(), total_read_size_.load(), total_write_size_.load(),
              readable_size, ring_buf_.get_head(), remote_head, remote_tail_,
              pending_write_num_data_.load(), pending_write_num_status_.load(),
              get_status());

      if (strcmp(last_msg, msg) != 0) {
        gpr_log(GPR_INFO, "Pair %p %s", this, msg);
        strcpy(last_msg, msg);
      }
      sleep(1);
    }
    gpr_log(GPR_INFO, "Monitor Exits");
  }
};

class PairPool {
  static constexpr int kInitPoolSize = 128;

  PairPool() {
    struct grpc_core::ibverbs::attr attr;
    attr.port = 1;  // TODO: read from config
    attr.index = 0;
    dev_ = CreateDevice(attr);
  }

 public:
  PairPool(const PairPool&) = delete;

  PairPool& operator=(const PairPool&) = delete;

  static PairPool& Get() {
    static PairPool pool;
    return pool;
  }

  PairPollable* Take() {
    std::lock_guard<std::mutex> lg(mu_);

    if (pairs_.empty()) {
      createPairs();
    }
    PairPollable* pair = pairs_.front();
    pairs_.pop();
    return pair;
  }

  void Putback(PairPollable* pair) {
    std::lock_guard<std::mutex> lg(mu_);

    pairs_.push(pair);
  }

 private:
  std::mutex mu_;
  std::queue<PairPollable*> pairs_;
  std::shared_ptr<Device> dev_;

  void createPairs() {
    for (int i = 0; i < kInitPoolSize; i++) {
      pairs_.push(new PairPollable(dev_));
    }
  }
};

}  // namespace ibverbs
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_IBVERBS_PAIR_H
