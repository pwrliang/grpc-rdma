#include <iomanip>
#include <sstream>
#include <thread>
#include "RDMASenderReceiver.h"
#include "fcntl.h"
#include "log.h"

// -----< RDMASenderReceiverBP >-----

std::string time_in_HH_MM_SS_MMM() {
  using namespace std::chrono;

  // get current time
  auto now = system_clock::now();

  // get number of milliseconds for the current second
  // (remainder after division into seconds)
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

  // convert to std::time_t in order to convert to std::tm (broken time)
  auto timer = system_clock::to_time_t(now);

  // convert to broken time
  std::tm bt = *std::localtime(&timer);

  std::ostringstream oss;

  oss << std::put_time(&bt, "%H:%M:%S");  // HH:MM:SS
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

  return oss.str();
}

RDMASenderReceiverBP::RDMASenderReceiverBP() {
  auto pd = node_.get_pd();

  ringbuf_bp_ = new RingBufferBP(ringbuf_sz_);
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_bp_->get_buf(), ringbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverBP::RDMASenderReceiverBP, failed to local_reg "
             "local_ringbuf_mr");
    exit(-1);
  }

  ringbuf_ = ringbuf_bp_;
  max_send_size_ = sendbuf_sz_ - sizeof(size_t) - 1;
  checked_.store(false);

  rdma_log(RDMA_DEBUG, "RDMASenderReceiverBP %p created", this);
  last_send_time_ =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  last_recv_time_ =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  total_recv_sz = 0;
  total_send_sz = 0;
  monitor_ = std::thread([this]() {
    while (true) {
      auto current = std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now());
      int time_out = 5;
      if (true) {
        printf(
            "%s %p, total send: %zu, total recv: %zu, local head: %zu, remote "
            "tail: %zu\n",
            time_in_HH_MM_SS_MMM().c_str(), this, total_send_sz.load(),
            total_recv_sz.load(), ringbuf_->get_head(), remote_ringbuf_tail_);
      }
      //      if (true) {
      //        printf("%p Recv timeout, total send: %zu, total recv: %zu\n",
      //               this, total_send_sz.load(), total_recv_sz.load());
      //      }

      sleep(5);
    }
  });
}

RDMASenderReceiverBP::~RDMASenderReceiverBP() {
  delete conn_data_bp_;
  delete ringbuf_bp_;
}

void RDMASenderReceiverBP::connect(int fd) {
  RDMASenderReceiver::connect(fd);
  conn_data_bp_ = new RDMAConnBP(fd, &node_);
  conn_data_bp_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);

  rdma_log(RDMA_DEBUG, "RDMASenderReceiverBP connected");
  // printf("RDMASenderReceiverBP is connected\n");
  connected_ = true;
}

bool RDMASenderReceiverBP::check_incoming() {
  // previous checked_ is true (new incoming data already found by another
  // thread), keep checked_ true, return false, avoid call fd_become_readable
  // twice for the same incoming data
  if (checked_.exchange(true)) return false;
  // previous checked_ is false (no incoming data found before),
  // set checked_ true to prevent other thread entering

  // found new incoming data,
  // return true, so fd_become_readable will be called, then
  // RDMASenderReceiverBP::recv will be called leave checked_ true. after recv
  // finished, set checked_ to false. if (ringbuf_bp_->check_head()) return
  // true;
  if (ringbuf_bp_->check_mlen()) return true;

  // no new incoming data found, return false,
  // leave checked_ false, so other thread could check again
  return !checked_.exchange(false);
}

size_t RDMASenderReceiverBP::check_and_ack_incomings_locked() {
//  unread_mlens_ = ringbuf_bp_->check_mlens();
   unread_mlens_ = ringbuf_bp_->check_mlen();
  return unread_mlens_;
}

size_t RDMASenderReceiverBP::recv(msghdr* msg, size_t msghdr_size) {
  // the actual read size is unread_data_size
  size_t mlens = unread_mlens_;

  // since we may read more data than unread_mlens_, mlens will be updated to
  // the real mlens we have read
  size_t lens = ringbuf_bp_->read_to_msghdr(msg, msghdr_size, mlens);
  if (lens == 0) {
    rdma_log(RDMA_WARNING, "RDMASenderReceiverBP::recv, lens == 0");
    exit(1);
    return 0;
  }

  checked_.store(false);
  unread_mlens_ = 0;
  garbage_ += lens;
  total_recv_sz += lens;
  //  if (garbage_ >= ringbuf_sz_ / 2) {  // garbage_ >= ringbuf_sz_ / 2
  update_remote_metadata();
  garbage_ = 0;
  //  }
  last_recv_time_ =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  printf(
      "%s %p, RECV: %zu total send: %zu, total recv: %zu, local head: %zu, "
      "remote "
      "tail: %zu\n",
      time_in_HH_MM_SS_MMM().c_str(), this, mlens, total_send_sz.load(),
      total_recv_sz.load(), ringbuf_->get_head(), remote_ringbuf_tail_);

  return mlens;
}


// mlen <= sendbuf_sz_ - sizeof(size_t) - 1;
bool RDMASenderReceiverBP::send(msghdr* msg, size_t mlen) {
  if (mlen + sizeof(size_t) + 1 > sendbuf_sz_) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverBP::send, mlen > sendbuf size, %zu vs %zu",
             mlen + sizeof(size_t) + 1, sendbuf_sz_);
    return false;
    exit(1);
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(size_t) + 1;

  update_local_metadata();
  size_t used =
      (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) %
      remote_ringbuf_sz;
  // If unread datasize + the size of data we want to send is greater than ring
  // buffer size, we can not send message. we reserve 1 byte to distinguish the
  // status between empty and full

  printf(
      "%s %p, TRY SEND: %zu total send: %zu, total recv: %zu, local head: %zu, "
      "remote "
      "tail: %zu\n",
      time_in_HH_MM_SS_MMM().c_str(), this, mlen, total_send_sz.load(),
      total_recv_sz.load(), ringbuf_->get_head(), remote_ringbuf_tail_);

  if (used + len > remote_ringbuf_sz - 10) {
    printf("used: %zu len: %zu used+len: %zu vs %zu\n", used, len, used + len,
           remote_ringbuf_sz - 10);
    return false;
  }
  last_send_time_ =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  *(size_t*)sendbuf_ = mlen;
  uint8_t* start = sendbuf_ + sizeof(size_t);
  size_t iov_idx, nwritten;
  for (iov_idx = 0, nwritten = 0; iov_idx < msg->msg_iovlen && nwritten < mlen;
       iov_idx++) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    nwritten += iov_len;
    if (nwritten <= sendbuf_sz_) {
      memcpy(start, iov_base, iov_len);
    } else {
      rdma_log(RDMA_ERROR,
               "RDMASenderReceiverBP::send, nwritten = %d, sendbuf size = %d",
               nwritten, sendbuf_sz_);
      return false;
    }
    start += iov_len;
  }
  // *start = 1;
  sendbuf_[len - 1] = 1;

  if (mlen == 102) {
    int last_pos = 0;
    printf("SEND:");
    print_ringbuf(last_pos, sendbuf_ , mlen);
  }

  if (iov_idx != msg->msg_iovlen || nwritten != mlen) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverBP::send, iov_idx = %d, msg_iovlen = %d, "
             "nwritten = %d, mlen = %d",
             iov_idx, msg->msg_iovlen, nwritten, mlen);
    exit(-1);
  }

  conn_data_bp_->post_send_and_poll_completion(
      remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, 0, len,
      IBV_WR_RDMA_WRITE, false);
  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + len) % remote_ringbuf_sz;
  total_send_sz += len;


  printf(
      "%s %p, SEND: %zu total send: %zu, total recv: %zu, local head: %zu, "
      "remote "
      "tail: %zu\n",
      time_in_HH_MM_SS_MMM().c_str(), this, mlen, total_send_sz.load(),
      total_recv_sz.load(), ringbuf_->get_head(), remote_ringbuf_tail_);
  return true;
}