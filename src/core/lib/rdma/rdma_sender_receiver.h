#ifndef GRPC_CORE_LIB_RDMA_RDMA_SENDER_RECEIVER_H
#define GRPC_CORE_LIB_RDMA_RDMA_SENDER_RECEIVER_H

#include <sys/epoll.h>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include "grpcpp/get_clock.h"
#include "grpcpp/stats_time.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/rdma/rdma_conn.h"
#include "src/core/lib/rdma/ringbuffer.h"
#define RDMA_DETECT_CONTENTION
#define RDMA_MAX_WRITE_IOVEC 1024

const size_t DEFAULT_HEADBUF_SZ = 64;

#ifdef RDMA_DETECT_CONTENTION
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

class RDMASenderReceiver {
 public:
  explicit RDMASenderReceiver(RDMAConn* conn_data, RingBuffer* ringbuf,
                              RDMAConn* conn_metadata, bool server)
      : conn_data_(conn_data),
        ringbuf_(ringbuf),
        conn_metadata_(conn_metadata),
        server_(server),
        status_(Status::kNew),
        remote_ringbuf_tail_(0),
        unread_mlens_(0),
        metadata_recvbuf_sz_(DEFAULT_HEADBUF_SZ),
        metadata_sendbuf_sz_(DEFAULT_HEADBUF_SZ),
        n_outstanding_send_(0),
        bytes_outstanding_send_(0),
        last_failed_send_size_(0),
        read_content_conter_(0),
        write_content_counter_(0),
        read_counter_(0),
        write_counter_(0),
        total_sent_(0),
        total_recv_(0),
        debug_(false),
        prev_remote_head_(0) {
    auto& node = RDMANode::GetInstance();
    auto pd = node.get_pd();
    size_t sendbuf_size = ringbuf->get_sendbuf_size();

    send_chunk_size_ = MIN(RDMAConfig::GetInstance().get_send_chunk_size(), sendbuf_size - sizeof(size_t) - 1);

    sendbuf_ = new uint8_t[sendbuf_size];

    if (sendbuf_mr_.RegisterLocal(pd, sendbuf_, sendbuf_size)) {
      gpr_log(GPR_ERROR, "failed to RegisterLocal sendbuf_mr");
      exit(-1);
    }

    // Enable by default
    if (RDMAConfig::GetInstance().is_zero_copy()) {
      zerocopy_ = true;
      last_zerocopy_send_finished_ = true;
      zerocopy_sendbuf_ = new uint8_t[sendbuf_size];
      if (zerocopy_sendbuf_mr_.RegisterLocal(pd, zerocopy_sendbuf_,
                                             sendbuf_size)) {
        gpr_log(GPR_ERROR, "failed to RegisterLocal zerocopy_sendbuf_mr");
        abort();
      }
    } else {
      zerocopy_ = false;
      last_zerocopy_send_finished_ = false;
      zerocopy_sendbuf_ = nullptr;
    }
    unfinished_zerocopy_send_size_ = 0;

    posix_memalign(&metadata_recvbuf_, 64, metadata_recvbuf_sz_);
    memset(metadata_recvbuf_, 0, metadata_recvbuf_sz_);
    if (local_metadata_recvbuf_mr_.RegisterLocal(pd, metadata_recvbuf_,
                                                 metadata_recvbuf_sz_)) {
      gpr_log(GPR_ERROR, "failed to RegisterLocal local_metadata_recvbuf_mr");
      abort();
    }

    posix_memalign(&metadata_sendbuf_, 64, metadata_sendbuf_sz_);
    memset(metadata_sendbuf_, 0, metadata_sendbuf_sz_);
    if (metadata_sendbuf_mr_.RegisterLocal(pd, metadata_sendbuf_,
                                           metadata_sendbuf_sz_)) {
      gpr_log(GPR_ERROR, "failed to RegisterLocal metadata_sendbuf_mr");
      abort();
    }
    int no_cpu_freq_fail = 0;
    mhz_ = get_cpu_mhz(no_cpu_freq_fail);
  }

  virtual void Init() = 0;

  virtual void Shutdown() {
    gpr_log(GPR_INFO, "Shutdown %p", this);
    status_ = Status::kShutdown;
  }

  virtual ~RDMASenderReceiver() {
    gpr_log(
        GPR_INFO,
        "total sent = %lld, total zerocopy sent = %lld, zerocopy ratio = %7.3f",
        total_sent_.load(), total_zerocopy_send_size,
        double(total_zerocopy_send_size) / total_sent_.load());
    status_ = Status::kDisconnected;
    delete[] sendbuf_;
    delete[] zerocopy_sendbuf_;
    free(metadata_recvbuf_);
    free(metadata_sendbuf_);
  }

  bool is_server() const { return server_; }

  size_t get_max_send_size() const { return ringbuf_->get_max_send_size(); }

  double get_mhz() const { return mhz_; }

  virtual int Send(msghdr* msg, ssize_t* sz) = 0;

  virtual int Recv(msghdr* msg, ssize_t* sz) = 0;

  virtual size_t MarkMessageLength() = 0;

  void pollLastSendCompletion() {
    uint32_t n_poll = n_outstanding_send_.exchange(0);
    bytes_outstanding_send_.exchange(0);

    if (n_poll > 0) {
      GRPCProfiler profiler(GRPC_STATS_TIME_SEND_POLL, 0);
      int ret = conn_data_->PollSendCompletion(n_poll);

      if (ret != 0) {
        gpr_log(GPR_ERROR,
                "rdmasr: %p pollLastSendCompletion failed, code: %d ", this,
                ret);
        abort();
      }
    }
  }

  void* RequireZerocopySendSpace(size_t size) {
    pollLastSendCompletion();
    if (!zerocopy_ || size >= ringbuf_->get_sendbuf_size() - 64) {
      return nullptr;
    }

    size_t max_counter = 1;
    while (!last_zerocopy_send_finished_.exchange(false)) {
      if (max_counter-- == 1) {
        // printf("require %lld send bytes, denied\n", size);
        return nullptr;
      }
    }
    unfinished_zerocopy_send_size_.fetch_add(size);
    return (void*)(zerocopy_sendbuf_);
  }

  bool HasPendingWrite() const { return last_failed_send_size_ > 0; }

 protected:
  virtual int updateRemoteMetadata() = 0;

  bool ZerocopySendbufContains(void* bytes) {
    if (!zerocopy_) return false;
    uint8_t* ptr = (uint8_t*)bytes;
    return (ptr >= zerocopy_sendbuf_ &&
            ptr < (zerocopy_sendbuf_ + ringbuf_->get_sendbuf_size()));
  }

  virtual size_t get_remote_ringbuf_head() {
    MEM_BAR()
    size_t curr_head = static_cast<volatile size_t*>(metadata_recvbuf_)[0];
    MEM_BAR()
    if (curr_head != prev_remote_head_) {
      gpr_log(GPR_INFO, "%c received new head, %zu->%zu",
              is_server() ? 'S' : 'C', prev_remote_head_, curr_head);
      prev_remote_head_ = curr_head;
    }
    return curr_head;
  }

  virtual bool isWritable(size_t mlen) = 0;

  enum class Status { kNew, kConnected, kShutdown, kDisconnected };

  RDMAConn* conn_data_;
  RingBuffer* ringbuf_;
  RDMAConn* conn_metadata_;
  Status status_;

  size_t remote_ringbuf_tail_;
  MemRegion local_ringbuf_mr_, remote_ringbuf_mr_;
  std::atomic_uint32_t unread_mlens_;

  uint8_t *sendbuf_, *zerocopy_sendbuf_;
  MemRegion sendbuf_mr_, zerocopy_sendbuf_mr_;
  std::atomic_uint32_t n_outstanding_send_;
  std::atomic_uint32_t bytes_outstanding_send_;
  std::atomic_uint32_t last_failed_send_size_;
  size_t send_chunk_size_;

  bool zerocopy_;
  std::atomic_bool last_zerocopy_send_finished_;
  std::atomic_size_t unfinished_zerocopy_send_size_;
  size_t total_zerocopy_send_size = 0;

  size_t metadata_recvbuf_sz_;
  void* metadata_recvbuf_;
  MemRegion local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_;

  size_t metadata_sendbuf_sz_;
  void* metadata_sendbuf_;
  MemRegion metadata_sendbuf_mr_;

  // for debugging
  std::atomic_int read_content_conter_, write_content_counter_;
  std::atomic_int read_counter_, write_counter_;
  std::atomic_size_t total_sent_, total_recv_;
  std::atomic_bool debug_;
  std::thread debug_thread_;
  size_t prev_remote_head_;
  double mhz_;

 private:
  bool server_;
};

/*
 * 1. update_remote_metadata after garbage >= ringbuf_size_ / 2, so
 * sendbuf_size_ <= ringbuf_size_ / 2.
 * 2. reset ringbuf fisrt, then update head.
 * 3. mlen: length of pure data; len: mlen + sizeof(size_t) + 1.
 */

class RDMASenderReceiverBP : public RDMASenderReceiver {
 public:
  RDMASenderReceiverBP(int fd, bool server);

  ~RDMASenderReceiverBP();

  void Init() override;

  int Send(msghdr* msg, ssize_t* sz) override;

  int SendChunk(msghdr* msg, ssize_t* sz);

  int Recv(msghdr* msg, ssize_t* sz) override;

  // this should be thread safe,
  bool HasMessage() const;

  size_t MarkMessageLength() override;

  int ToEpollEvent() {
    uint32_t event = 0;

    if (dynamic_cast<RingBufferBP*>(ringbuf_)->CheckFirstMessageLength() > 0) {
      event |= EPOLLIN;
    }

    if (last_failed_send_size_ > 0 && isWritable(last_failed_send_size_)) {
      event |= EPOLLOUT;
    }

    if (status_ == RDMASenderReceiver::Status::kShutdown) {
      event |= EPOLLHUP;
    }

    return event;
  }

  size_t get_max_send_chunk_size() {
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - get_remote_ringbuf_head()) %
        remote_ringbuf_sz;
    size_t max_send_size = remote_ringbuf_sz - sizeof(size_t) - 1 - used;
    size_t avail_send_size = ringbuf_->get_sendbuf_size() - bytes_outstanding_send_.load();
    return MIN(MIN(max_send_size, send_chunk_size_), avail_send_size);
  }

 private:
  bool isWritable(size_t mlen) override {
    size_t len = mlen + sizeof(size_t) + 1;
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - get_remote_ringbuf_head()) %
        remote_ringbuf_sz;
    // If unread datasize + the size of data we want to send is greater than
    // ring buffer size, we can not send message. we reserve 1 byte to
    // distinguish the status between empty and full

    return used + len <= remote_ringbuf_sz - 8;
  }

  int updateRemoteMetadata() override {
    MEM_BAR();
    *static_cast<volatile size_t*>(metadata_sendbuf_) = ringbuf_->get_head();
    MEM_BAR();
    int n_entries = conn_metadata_->PostSendRequest(
        remote_metadata_recvbuf_mr_, metadata_sendbuf_mr_, metadata_sendbuf_sz_,
        IBV_WR_RDMA_WRITE);
    MEM_BAR();
    int ret = conn_metadata_->PollSendCompletion(n_entries);
    MEM_BAR();
    if (ret != 0) {
      gpr_log(GPR_ERROR,
              "updateRemoteMetadata failed, code: %d "
              "head: %zu, post_num: %d",
              ret, reinterpret_cast<size_t*>(metadata_sendbuf_)[0], n_entries);
      return EPIPE;
    }
    return 0;
  }
};

class RDMASenderReceiverBPEV : public RDMASenderReceiverBP {
 public:
  explicit RDMASenderReceiverBPEV(int fd, bool server);

  ~RDMASenderReceiverBPEV();

  void Init() override;

  int get_wakeup_fd() const { return wakeup_fd_; }

  void set_index(int index) { this->index_ = index; }

  int get_index() const { return index_; }

 private:
  // this need to sync in initialization
  int wakeup_fd_;
  int index_;
};

class RDMASenderReceiverEvent : public RDMASenderReceiver {
 public:
  explicit RDMASenderReceiverEvent(int fd, bool server);

  ~RDMASenderReceiverEvent();

  void Init() override;

  int Send(msghdr* msg, ssize_t* sz) override;

  int Recv(msghdr* msg, ssize_t* sz) override;

  size_t MarkMessageLength() override;

  int get_recv_channel_fd() const { return conn_data_->get_recv_channel_fd(); }

  int get_metadata_recv_channel_fd() const {
    return conn_metadata_->get_recv_channel_fd();
  }

  void SetDataReady() { data_ready_ = true; }

  void SetMetadataReady() { metadata_ready_ = true; }

 private:
  int updateRemoteMetadata() override {
    reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
    reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = conn_data_->get_rr_tail();
    int n_entries = conn_metadata_->PostSendRequest(
        remote_metadata_recvbuf_mr_, metadata_sendbuf_mr_, metadata_sendbuf_sz_,
        IBV_WR_RDMA_WRITE_WITH_IMM);
    int ret = conn_metadata_->PollSendCompletion(n_entries);

    if (ret != 0) {
      gpr_log(GPR_ERROR,
              "PollSendCompletion failed, code: %d "
              "remote_ringbuf_tail = "
              "%zu, post_num = %d",
              ret, remote_ringbuf_tail_, n_outstanding_send_.load());
      return EPIPE;
    }

    return 0;
  }

  bool isWritable(size_t mlen) override {
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - get_remote_ringbuf_head()) %
        remote_ringbuf_sz;
    size_t remote_rr_tail = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
    size_t avail_rr_num =
        (remote_rr_tail - remote_rr_head_ + DEFAULT_MAX_POST_RECV) %
        DEFAULT_MAX_POST_RECV;
    if (avail_rr_num == 0) {
      return false;
    }

    return used + mlen <= remote_ringbuf_sz - 8;
  }

  size_t remote_rr_head_ = 0;
  std::atomic_bool data_ready_, metadata_ready_;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMA_SENDER_RECEIVER_H
