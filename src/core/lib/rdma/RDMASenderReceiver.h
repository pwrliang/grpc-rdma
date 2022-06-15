#ifndef _RDMASENDERRECEIVER_H_
#define _RDMASENDERRECEIVER_H_

#include <sys/epoll.h>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>
#include "RDMAConn.h"
#include "grpcpp/get_clock.h"
#include "ringbuffer.h"
const size_t DEFAULT_RINGBUF_SZ = 1024ull * 1024 * 10;
const size_t DEFAULT_HEADBUF_SZ = 64;

class RDMASenderReceiver {
 public:
  explicit RDMASenderReceiver(RDMAConn* conn, RingBuffer* ringbuf)
      : conn_(conn),
        ringbuf_(ringbuf),
        remote_ringbuf_head_(0),
        remote_ringbuf_tail_(0),
        unread_mlens_(0),
        metadata_recvbuf_sz_(DEFAULT_HEADBUF_SZ),
        metadata_sendbuf_sz_(DEFAULT_HEADBUF_SZ),
        connected_(false),
        last_send_time_(0),
        last_recv_time_(0) {
    auto& node = RDMANode::GetInstance();
    auto pd = node.get_pd();
    size_t sendbuf_size = ringbuf->get_sendbuf_size();

    sendbuf_ = new uint8_t[sendbuf_size];

    if (sendbuf_mr_.local_reg(pd, sendbuf_, sendbuf_size)) {
      gpr_log(GPR_ERROR, "failed to local_reg sendbuf_mr");
      exit(-1);
    }

    // char* flag = getenv("GRPC_RDMA_ZEROCOPY_ENABLE");
    // if (flag && strcmp(flag, "true") == 0) {
    zerocopy_flag_ = true;
    last_zerocopy_send_finished_.store(true);
    zerocopy_sendbuf_ = new uint8_t[sendbuf_size];
    if (zerocopy_sendbuf_mr_.local_reg(pd, zerocopy_sendbuf_, sendbuf_size)) {
      gpr_log(GPR_ERROR, "failed to local_reg zerocopy_sendbuf_mr");
      exit(-1);
    }
    // } else {
    //   zerocopy_flag_ = false;
    //   last_zerocopy_send_finished_.store(false);
    // }
    unfinished_zerocopy_send_size_.store(0);

    posix_memalign(&metadata_recvbuf_, 64, metadata_recvbuf_sz_);
    memset(metadata_recvbuf_, 0, metadata_recvbuf_sz_);
    if (local_metadata_recvbuf_mr_.local_reg(pd, metadata_recvbuf_,
                                             metadata_recvbuf_sz_)) {
      gpr_log(GPR_ERROR, "failed to local_reg local_metadata_recvbuf_mr");
      exit(-1);
    }

    posix_memalign(&metadata_sendbuf_, 64, metadata_sendbuf_sz_);
    memset(metadata_sendbuf_, 0, metadata_sendbuf_sz_);
    if (metadata_sendbuf_mr_.local_reg(pd, metadata_sendbuf_,
                                       metadata_sendbuf_sz_)) {
      gpr_log(GPR_ERROR, "failed to local_reg metadata_sendbuf_mr");
      exit(-1);
    }
  }

  virtual void Shutdown() {
    update_local_metadata();
    if (remote_exit_ == 1) return;
    reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
    reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = 1;
    int n_entries =
        conn_->post_send(remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_,
                         0, metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
    conn_->poll_send_completion(n_entries);
  }

  virtual ~RDMASenderReceiver() {
    delete[] sendbuf_;
    free(metadata_recvbuf_);
    free(metadata_sendbuf_);
  }

  size_t get_unread_data_size() const { return unread_mlens_; }

  size_t get_max_send_size() const { return ringbuf_->get_max_send_size(); }

  bool get_zerocopy_flag() const { return zerocopy_flag_; }

  cycles_t last_send_time() const { return last_send_time_; }

  cycles_t last_recv_time() const { return last_recv_time_; }

  virtual bool send(msghdr* msg, size_t mlen) = 0;
  virtual size_t recv(msghdr* msg) = 0;

  void WaitConnect() const {
    while (!connected_) {
      std::this_thread::yield();
    }
  }

  bool IfRemoteExit() { return remote_exit_ == 1; }

  void* require_zerocopy_sendspace(size_t size) {
    if (!zerocopy_flag_ || size >= ringbuf_->get_sendbuf_size() - 64) {
      return nullptr;
    }

    size_t max_counter = 1;
    while (last_zerocopy_send_finished_.exchange(false) == false) {
      if (max_counter-- == 1) {
        // printf("require %lld send bytes, denied\n", size);
        return nullptr;
      }
    }
    unfinished_zerocopy_send_size_.fetch_add(size);
    return (void*)(zerocopy_sendbuf_);
  }

  bool zerocopy_sendbuf_contains(void* bytes) {
    if (!zerocopy_flag_) return false;
    uint8_t* ptr = (uint8_t*)bytes;
    return (ptr >= zerocopy_sendbuf_ &&
            ptr < (zerocopy_sendbuf_ + ringbuf_->get_sendbuf_size()));
  }

 protected:
  virtual void connect(int fd) = 0;
  virtual void update_remote_metadata() {
    reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
    reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = 0;
    int n_entries =
        conn_->post_send(remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_,
                         0, metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
    conn_->poll_send_completion(n_entries);
  }
  virtual void update_local_metadata() {
    remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
    remote_exit_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
  }
  virtual bool is_writable(size_t mlen) = 0;

  RDMAConn* conn_;
  RingBuffer* ringbuf_;

  size_t remote_ringbuf_head_, remote_ringbuf_tail_;
  MemRegion local_ringbuf_mr_, remote_ringbuf_mr_;
  std::atomic_uint32_t unread_mlens_;

  uint8_t *sendbuf_, *zerocopy_sendbuf_;
  MemRegion sendbuf_mr_, zerocopy_sendbuf_mr_;
  bool zerocopy_flag_;
  std::atomic_bool last_zerocopy_send_finished_;
  std::atomic_size_t unfinished_zerocopy_send_size_;
  size_t total_send_size = 0, total_zerocopy_send_size = 0;

  size_t metadata_recvbuf_sz_;
  void* metadata_recvbuf_;
  MemRegion local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_;

  size_t metadata_sendbuf_sz_;
  void* metadata_sendbuf_;
  MemRegion metadata_sendbuf_mr_;

  // Profiling and limit wakeup
  cycles_t last_send_time_;
  cycles_t last_recv_time_;

  bool connected_;
  int remote_exit_ = 0;
  int fd_;
};

/*
 * 1. update_remote_metadata after garbage >= ringbuf_size_ / 2, so
 * sendbuf_size_ <= ringbuf_size_ / 2.
 * 2. reset ringbuf fisrt, then update head.
 * 3. mlen: length of pure data; len: mlen + sizeof(size_t) + 1.
 */

class RDMASenderReceiverBP : public RDMASenderReceiver {
 public:
  RDMASenderReceiverBP();
  ~RDMASenderReceiverBP();

  void connect(int fd) override;

  bool send(msghdr* msg, size_t mlen) override;

  size_t recv(msghdr* msg) override;

  bool if_write_again() {
    return write_again_;
  }  // if previous is true, only one thread return true
  void write_again() { write_again_ = true; }
  void write_again_done() { write_again_ = false; }

  bool is_writable(size_t mlen) override {
    size_t len = mlen + sizeof(size_t) + 1;
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) %
        remote_ringbuf_sz;
    // If unread datasize + the size of data we want to send is greater than
    // ring buffer size, we can not send message. we reserve 1 byte to
    // distinguish the status between empty and full
    if (remote_exit_) {
      // printf("fd = %d, remote exit\n", fd_);
      return false;
    }

    return used + len <= remote_ringbuf_sz - 8;
  }
  // this should be thread safe,
  bool check_incoming() const;

  size_t check_and_ack_incomings_locked(bool read_all = true);

 protected:
  int last_n_post_send_;
  std::atomic_bool write_again_;
  std::thread conn_th_;
};

class RDMASenderReceiverEvent : public RDMASenderReceiver {
 public:
  RDMASenderReceiverEvent();
  ~RDMASenderReceiverEvent();

  // create channel for each rdmasr.
  void connect(int fd) override;
  void update_remote_metadata() override;
  void update_local_metadata() override;
  void Shutdown() override;
  bool connected() { return connected_; }

  bool send(msghdr* msg, size_t mlen) override;
  virtual size_t recv(msghdr* msg);

  void check_data() { check_data_ = true; }
  void check_metadata() { check_metadata_ = true; }

  bool is_writable(size_t mlen) override {
    update_local_metadata();  // update remote_ringbuf_head_ and remote_rr_tail
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
    if (used + mlen > remote_ringbuf_sz - 8) {
      return false;
    }
    if (remote_exit_) {
      // printf("fd = %d, remote exit\n", fd_);
      return false;
    }
    return true;
  }

  bool is_writable() {
    return last_failed_send_size_ > 0 && is_writable(last_failed_send_size_);
  }

  size_t check_and_ack_incomings_locked();

  int get_recv_channel_fd() const { return conn_->get_recv_channel_fd(); }

  int get_metadata_recv_channel_fd() const {
    return conn_metadata_->get_recv_channel_fd();
  }

 protected:
  RDMAConn* conn_metadata_ = nullptr;

  int last_n_post_send_;

  // this need to sync in initialization
  size_t remote_rr_tail_ = 0, remote_rr_head_ = 0;
  size_t last_failed_send_size_;

#ifdef SENDER_RECEIVER_NON_ATOMIC
  bool check_data_;
  bool check_metadata_;
#else
  std::atomic_bool check_data_;
  std::atomic_bool check_metadata_;
#endif

  std::thread conn_th_;
};

class RDMASenderReceiverBPEV : public RDMASenderReceiver {
 public:
  RDMASenderReceiverBPEV();
  ~RDMASenderReceiverBPEV();

  // create channel for each rdmasr.
  void connect(int fd) override;
  void update_remote_metadata() override;
  void update_local_metadata() override;
  bool connected() { return connected_; }

  bool send(msghdr* msg, size_t) override;

  virtual size_t recv(msghdr* msg);

  bool is_writable(size_t mlen) override {
    update_local_metadata();  // update remote_ringbuf_head_ and remote_rr_tail
    size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
    size_t used =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) %
        remote_ringbuf_sz;

    if (used + mlen > remote_ringbuf_sz - 8) {
      return false;
    }
    if (remote_exit_) {
      // printf("fd = %d, remote exit\n", fd_);
      return false;
    }
    return true;
  }

  bool is_writable() {
    return last_failed_send_size_ > 0 && is_writable(last_failed_send_size_);
  }

  size_t check_and_ack_incomings_locked();

  int get_wakeup_fd() const { return wakeup_fd_; }

  bool check_incoming() const;

  void set_index(int index) { this->index_ = index; }

  int get_index() const { return index_; }

  bool set_event() { return has_event_.test_and_set(); }

 private:
  int last_n_post_send_;
  // this need to sync in initialization
  size_t last_failed_send_size_;
  int wakeup_fd_;
  int index_;
  std::thread conn_th_;
  std::atomic_flag has_event_;
};
#endif