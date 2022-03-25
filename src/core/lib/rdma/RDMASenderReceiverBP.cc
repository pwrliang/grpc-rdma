#include <iomanip>
#include <sstream>
#include <thread>
#include "RDMASenderReceiver.h"
#include "fcntl.h"
#include "log.h"

// -----< RDMASenderReceiverBP >-----

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

  rdma_log(RDMA_DEBUG, "RDMASenderReceiverBP %p created", this);
  total_recv_sz = 0;
  total_send_sz = 0;
}

RDMASenderReceiverBP::~RDMASenderReceiverBP() {
  delete conn_data_bp_;
  delete ringbuf_bp_;
}

void RDMASenderReceiverBP::connect(int fd) {
  conn_th_ = std::thread([this, fd]() {
    RDMASenderReceiver::connect(fd);
    conn_data_bp_ = new RDMAConnBP(fd, &node_);
    conn_data_bp_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);
    rdma_log(RDMA_DEBUG, "RDMASenderReceiverBP connected");
    // printf("RDMASenderReceiverBP is connected\n");
    connected_ = true;
  });
  conn_th_.detach();
}

bool RDMASenderReceiverBP::check_incoming() {
  while (!connected()) {
    std::this_thread::yield();
  }
  return ringbuf_bp_->check_mlen();
}

size_t RDMASenderReceiverBP::check_and_ack_incomings_locked() {
  while (!connected()) {
    std::this_thread::yield();
  }
  unread_mlens_ = ringbuf_bp_->check_mlens();
  //  unread_mlens_ = ringbuf_bp_->check_mlen();
  return unread_mlens_;
}

size_t RDMASenderReceiverBP::recv(msghdr* msg, size_t msghdr_size) {
  while (!connected()) {
    std::this_thread::yield();
  }
  // the actual read size is unread_data_size
  size_t mlens = unread_mlens_;

  // since we may read more data than unread_mlens_, mlens will be updated to
  // the real mlens we have read
  size_t lens = ringbuf_bp_->read_to_msghdr(msg, msghdr_size, mlens);
  if (lens == 0) {
    rdma_log(RDMA_WARNING, "RDMASenderReceiverBP::recv, lens == 0");
    exit(1);
  }

  unread_mlens_ = 0;
  garbage_ += lens;
  total_recv_sz += lens;
  if (garbage_ >= ringbuf_sz_ / 2) {
    update_remote_metadata();
    garbage_ = 0;
  }

  return mlens;
}

// mlen <= sendbuf_sz_ - sizeof(size_t) - 1;
bool RDMASenderReceiverBP::send(msghdr* msg, size_t mlen) {
  while (!connected()) {
    std::this_thread::yield();
  }
  if (mlen + sizeof(size_t) + 1 > sendbuf_sz_) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverBP::send, mlen > sendbuf size, %zu vs %zu",
             mlen + sizeof(size_t) + 1, sendbuf_sz_);
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

  if (used + len > remote_ringbuf_sz - 8) {
    return false;
  }
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

  return true;
}