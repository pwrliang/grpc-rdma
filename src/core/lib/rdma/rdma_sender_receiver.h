#ifndef GRPC_CORE_LIB_RDMA_RDMA_SENDER_RECEIVER_H
#define GRPC_CORE_LIB_RDMA_RDMA_SENDER_RECEIVER_H

#include <sys/epoll.h>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include "grpcpp/get_clock.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/rdma/rdma_conn.h"
#include "src/core/lib/rdma/ringbuffer.h"
//#define RDMA_DETECT_CONTENTION
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
  explicit RDMASenderReceiver(RDMAConn* conn_data, RDMAConn* conn_metadata,
                              RingBuffer* ringbuf, bool server)
      : conn_data_(conn_data),
        conn_metadata_(conn_metadata),
        ringbuf_(ringbuf),
        server_(server),
        remote_ringbuf_head_(0),
        remote_ringbuf_tail_(0),
        unread_mlens_(0),
        metadata_recvbuf_sz_(DEFAULT_HEADBUF_SZ),
        metadata_sendbuf_sz_(DEFAULT_HEADBUF_SZ),
        n_outstanding_send_(0),
        last_failed_send_size_(0),
        read_counter_(0),
        write_counter_(0),
        debug_(false) {
    auto& node = RDMANode::GetInstance();
    auto pd = node.get_pd();
    size_t sendbuf_size = ringbuf->get_sendbuf_size();

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
  }

  virtual void Connect(int fd) = 0;

  virtual void Shutdown() {
    gpr_log(GPR_INFO, "RDMASenderReceiver shutdown");

    updateLocalMetadata();
    if (remote_exit_ == 1) return;
    reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
    reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = 1;
    int n_entries = conn_metadata_->PostSendRequest(
        remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
        metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
    conn_metadata_->PollSendCompletion(n_entries);
  }

  virtual ~RDMASenderReceiver() {
    delete[] sendbuf_;
    delete[] zerocopy_sendbuf_;
    free(metadata_recvbuf_);
    free(metadata_sendbuf_);
  }

  bool is_server() const { return server_; }

  size_t get_unread_message_length() const { return unread_mlens_; }

  size_t get_max_send_size() const { return ringbuf_->get_max_send_size(); }

  virtual bool Send(msghdr* msg, size_t mlen) = 0;

  virtual size_t Recv(msghdr* msg) = 0;

  virtual size_t MarkMessageLength() = 0;

  bool IfRemoteExit() { return remote_exit_ == 1; }

  void* RequireZerocopySendSpace(size_t size) {
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

  bool ZerocopySendbufContains(void* bytes) {
    if (!zerocopy_) return false;
    uint8_t* ptr = (uint8_t*)bytes;
    return (ptr >= zerocopy_sendbuf_ &&
            ptr < (zerocopy_sendbuf_ + ringbuf_->get_sendbuf_size()));
  }

  bool HasPendingWrite() const { return last_failed_send_size_ > 0; }

 protected:
  virtual void updateRemoteMetadata() {
    reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
    reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = 0;
    int n_entries = conn_metadata_->PostSendRequest(
        remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
        metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
    int ret = conn_metadata_->PollSendCompletion(n_entries);

    if (ret != 0) {
      gpr_log(GPR_ERROR,
              "updateRemoteMetadata failed, code: %d "
              "fd = %d, remote_ringbuf_tail = "
              "%zu, post_num = %d",
              ret, fd_, remote_ringbuf_tail_, n_outstanding_send_);
      abort();
    }
  }

  virtual void updateLocalMetadata() {
    remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
    remote_exit_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
  }

  virtual bool isWritable(size_t mlen) const = 0;

  RDMAConn* conn_data_;
  RDMAConn* conn_metadata_;
  RingBuffer* ringbuf_;

  size_t remote_ringbuf_head_, remote_ringbuf_tail_;
  MemRegion local_ringbuf_mr_, remote_ringbuf_mr_;
  std::atomic_uint32_t unread_mlens_;

  uint8_t *sendbuf_, *zerocopy_sendbuf_;
  MemRegion sendbuf_mr_, zerocopy_sendbuf_mr_;
  int n_outstanding_send_;
  std::atomic_uint32_t last_failed_send_size_;

  bool zerocopy_;
  std::atomic_bool last_zerocopy_send_finished_;
  std::atomic_size_t unfinished_zerocopy_send_size_;
  size_t total_send_size = 0, total_zerocopy_send_size = 0;

  size_t metadata_recvbuf_sz_;
  void* metadata_recvbuf_;
  MemRegion local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_;

  size_t metadata_sendbuf_sz_;
  void* metadata_sendbuf_;
  MemRegion metadata_sendbuf_mr_;

  int remote_exit_ = 0;
  int fd_;
  // for debugging
  std::atomic_int read_counter_, write_counter_;
  std::atomic_bool debug_;
  std::thread debug_thread_;

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
  RDMASenderReceiverBP(bool server);

  ~RDMASenderReceiverBP();

  void Connect(int fd) override;

  bool Send(msghdr* msg, size_t mlen) override;

  size_t Recv(msghdr* msg) override;

  // this should be thread safe,
  bool HasMessage() const;

  size_t MarkMessageLength() override;

 protected:
  bool isWritable(size_t mlen) const override {
    size_t len = mlen + sizeof(size_t) + 1;
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) %
        remote_ringbuf_sz;
    // If unread datasize + the size of data we want to send is greater than
    // ring buffer size, we can not send message. we reserve 1 byte to
    // distinguish the status between empty and full
    if (remote_exit_) {
      return false;
    }

    return used + len <= remote_ringbuf_sz - 8;
  }
};

class RDMASenderReceiverBPEV : public RDMASenderReceiver {
 public:
  explicit RDMASenderReceiverBPEV(bool server);

  ~RDMASenderReceiverBPEV();

  // create channel for each rdmasr.
  void Connect(int fd) override;

  bool Send(msghdr* msg, size_t) override;

  size_t Recv(msghdr* msg) override;

  bool HasMessage() const;

  size_t MarkMessageLength() override;

  int get_wakeup_fd() const { return wakeup_fd_; }

  void set_index(int index) { this->index_ = index; }

  int get_index() const { return index_; }

 private:
  // this need to sync in initialization
  int wakeup_fd_;
  int index_;

  bool isWritable(size_t mlen) const override {
    size_t len = mlen + sizeof(size_t) + 1;
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) %
        remote_ringbuf_sz;
    // If unread datasize + the size of data we want to send is greater than
    // ring buffer size, we can not send message. we reserve 1 byte to
    // distinguish the status between empty and full

    return used + len <= remote_ringbuf_sz - 8;
  }
};

class RDMASenderReceiverEvent : public RDMASenderReceiver {
 public:
  explicit RDMASenderReceiverEvent(bool server);

  ~RDMASenderReceiverEvent();

  // create channel for each rdmasr.
  void Connect(int fd) override;

  void Shutdown() override;

  bool Send(msghdr* msg, size_t mlen) override;
  size_t Recv(msghdr* msg) override;

  void CheckData() { check_data_ = true; }

  void CheckMetadata() { check_metadata_ = true; }

  size_t MarkMessageLength() override;

  int get_recv_channel_fd() const { return conn_data_->get_recv_channel_fd(); }

  int get_metadata_recv_channel_fd() const {
    return conn_metadata_->get_recv_channel_fd();
  }

 private:
  void updateRemoteMetadata() override;

  void updateLocalMetadata() override;

  bool isWritable(size_t mlen) const override {
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) %
        remote_ringbuf_sz;
    size_t avail_rr_num =
        (remote_rr_tail_ - remote_rr_head_ + DEFAULT_MAX_POST_RECV) %
        DEFAULT_MAX_POST_RECV;
    if (avail_rr_num == 0) {
      return false;
    }

    if (remote_exit_) {
      return false;
    }

    return used + mlen <= remote_ringbuf_sz - 8;
  }

  int last_n_post_send_;

  // this need to sync in initialization
  size_t remote_rr_tail_ = 0, remote_rr_head_ = 0;
#ifdef SENDER_RECEIVER_NON_ATOMIC
  bool check_data_;
  bool check_metadata_;
#else
  std::atomic_bool check_data_;
  std::atomic_bool check_metadata_;
#endif
};

#endif  // GRPC_CORE_LIB_RDMA_RDMA_SENDER_RECEIVER_H
