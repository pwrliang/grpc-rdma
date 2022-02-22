#include "RDMASenderReceiver.h"
#include "log.h"
#include "fcntl.h"
#include <thread>

// -----< RDMASenderReceiverBP >-----

RDMASenderReceiverBP::RDMASenderReceiverBP() {
  ibv_pd* pd = node_.get_pd();

  ringbuf_bp_ = new RingBufferBP(ringbuf_sz_);
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_bp_->get_buf(), ringbuf_sz_)) {
    rdma_log(RDMA_ERROR,
              "RDMASenderReceiverBP::RDMASenderReceiverBP, failed to local_reg "
              "local_ringbuf_mr");
    exit(-1);
  }

  ringbuf_ = ringbuf_bp_;
  max_send_size_ = sendbuf_sz_ - 2 * (sizeof(size_t) + 1) - 1;

  rdma_log(RDMA_DEBUG, "RDMASenderReceiverBP %p created", this);
}

RDMASenderReceiverBP::~RDMASenderReceiverBP() {
  if (conn_bp_) {
    delete conn_bp_;
  }
  if (ringbuf_bp_) {
    delete ringbuf_bp_;
  }
}

void RDMASenderReceiverBP::connect(int fd) {
  conn_bp_ = new RDMAConnBP(fd, &node_);
  conn_ = conn_bp_;
  conn_bp_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_bp_->sync_mr(local_head_recvbuf_mr_, remote_head_recvbuf_mr_);

  rdma_log(RDMA_DEBUG, "RDMASenderReceiverBP connected");
  connected_ = true;
}

bool RDMASenderReceiverBP::check_incoming() {
  return ringbuf_bp_->check_head();
}

size_t RDMASenderReceiverBP::check_and_ack_incomings() {
  unread_data_size_ = ringbuf_bp_->check_mlens();
  return unread_data_size_;
}

size_t RDMASenderReceiverBP::recv(msghdr* msg) {

  // the actual read size is unread_data_size
  size_t read_size = unread_data_size_;
  size_t mlens = ringbuf_bp_->read_to_msghdr(msg, unread_data_size_);
  if (mlens == 0) {
    rdma_log(RDMA_WARNING, "RDMASenderReceiverBP::recv, read_size == 0");
    return 0;
  }

  unread_data_size_ = 0;
  garbage_ += read_size;
  total_recv_sz += read_size;
  if (garbage_ > 0) { //garbage_ >= ringbuf_sz_ / 2
    update_remote_head();
    garbage_ = 0;
  }

  return mlens;
}

// this could be optimized.
// caller already checked msg
bool RDMASenderReceiverBP::send(msghdr* msg, size_t mlen) {
  if (mlen + sizeof(size_t) + 1 >= sendbuf_sz_) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverBP::send, mlen > sendbuf size, %zu vs %zu",
             mlen + sizeof(size_t) + 1, sendbuf_sz_);
    return false;
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(size_t) + 1;

  size_t used = (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) % remote_ringbuf_sz;
  while (used + len >= remote_ringbuf_sz - sizeof(size_t) - 1) {
    update_local_head();
    used = (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) % remote_ringbuf_sz;
    std::this_thread::yield();
    // printf("remote_ringbuf_head = %d\n", remote_ringbuf_head_);
  }

  *(size_t*)sendbuf_ = mlen;
  uint8_t* start = sendbuf_ + sizeof(size_t);
  for (size_t iov_idx = 0, nwritten = 0;
       iov_idx < msg->msg_iovlen && nwritten < mlen;
       iov_idx++) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    nwritten += iov_len;
    if (nwritten <= sendbuf_sz_) {
      memcpy(start, iov_base, iov_len);
    } else {
      rdma_log(RDMA_ERROR, "RDMASenderReceiverBP::send, nwritten = %d, sendbuf size = %d", nwritten, sendbuf_sz_);
      return false;
    }
    start += iov_len;
  }
  *start = 1;

  conn_bp_->post_send_and_poll_completion(remote_ringbuf_mr_, remote_ringbuf_tail_,
                                          sendbuf_mr_, 0, len, IBV_WR_RDMA_WRITE);
  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + len) % remote_ringbuf_sz;
  total_send_sz += len;
  return true;
}