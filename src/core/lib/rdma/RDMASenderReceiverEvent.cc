#include "RDMASenderReceiver.h"
#include "log.h"
#include "fcntl.h"
#include <thread>
#include <infiniband/verbs.h>

#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE (1 << 28)
#endif


// -----< RDMASenderReceiverEvent >-----

RDMASenderReceiverEvent::RDMASenderReceiverEvent() {
  ibv_pd* pd = node_.get_pd();

  ringbuf_event_ = new RingBufferEvent(ringbuf_sz_);
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_event_->get_buf(), ringbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverEvent::RDMASenderReceiverEvent, failed to local_reg "
             "local_ringbuf_mr");
    exit(-1);
  }

  ringbuf_ = ringbuf_event_;
  checked_.store(false);
  max_send_size_ = sendbuf_sz_ - 1;

  rdma_log(RDMA_INFO, "RDMASenderReceiverEvent %p created", this);
}

RDMASenderReceiverEvent::~RDMASenderReceiverEvent() {
  if (conn_data_event_) {
    delete conn_data_event_;
  }
  if (ringbuf_event_) {
    delete ringbuf_event_;
  }
}

void RDMASenderReceiverEvent::connect(int fd) {
  RDMASenderReceiver::connect(fd);
  conn_data_event_ = new RDMAConnEvent(fd, &node_, this);
  conn_data_ = conn_data_event_;
  conn_data_event_->sync_mr(local_ringbuf_mr_, remote_ringbuf_mr_);

  // there are at most DEFAULT_MAX_POST_RECV - 1 outstanding recv requests
  conn_data_event_->post_recvs(ringbuf_event_->get_buf(), ringbuf_sz_, local_ringbuf_mr_.lkey(), DEFAULT_MAX_POST_RECV - 1);
  update_remote_metadata(); // set remote_rr_tail

  char tmp;
  if (conn_data_event_->sync_data((char*)"s", &tmp, 1)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiverEvent::connect, failed to sync after connect");
    exit(-1);
  }

  rdma_log(RDMA_INFO, "RDMASenderReceiverEvent connected");
  connected_ = true;
}

void RDMASenderReceiverEvent::update_remote_metadata() {
  if (!ringbuf_ || !conn_metadata_) {
    rdma_log(RDMA_ERROR, "RDMASenderReceiver::update_remote_metadata, ringbuf or connector has not been initialized");
    exit(-1);
  }

  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_event_->head_;
  reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = conn_data_event_->rr_tail_;
  conn_metadata_->post_send_and_poll_completion(remote_metadata_recvbuf_mr_, 0,
                                       metadata_sendbuf_mr_, 0, 
                                       metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE, true);
  printf("update_remote, %lld, %lld\n", reinterpret_cast<size_t*>(metadata_sendbuf_)[0], reinterpret_cast<size_t*>(metadata_sendbuf_)[1]);
  rdma_log(RDMA_INFO, "RDMASenderReceiver::update_remote_metadata, %d, %d", 
           reinterpret_cast<size_t*>(metadata_sendbuf_)[0],
           reinterpret_cast<size_t*>(metadata_sendbuf_)[1]);
}

void RDMASenderReceiverEvent::update_local_metadata() {
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
  remote_rr_tail_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
}

bool RDMASenderReceiverEvent::check_incoming() {
  // previous checked_ is true (new incoming data already found by another thread), 
  // keep checked_ true, 
  // return false, avoid call fd_become_readable twice for the same incoming data
  if (checked_.exchange(true)) return false;
  // previous checked_ is false (no incoming data found before), 
  // set checked_ true to prevent other thread entering
  // now check incoming

  // found new incoming data, 
  // return true, so fd_become_readable will be called, then RDMASenderReceiverEvent::check_and_ack_incomings_locked will be called 
  // leave checked_ true. 
  // before check_and_ack_incomings_locked , set checked_ to false, 
  // so extra channel fd event will be detected, and extra check_and_ack_incomings_locked will be called
  // but RCQ event will not be missed
  if (conn_data_event_->get_event_locked()) return true;

  // no new incoming data found, return false, 
  // leave checked_ false, so other thread could check again
  return !checked_.exchange(false);
}

size_t RDMASenderReceiverEvent::check_and_ack_incomings_locked() {
  checked_.store(false);
  unread_mlens_ = conn_data_event_->get_events_locked(ringbuf_event_->buf_, ringbuf_sz_, local_ringbuf_mr_.lkey());
  if (conn_data_event_->rr_garbage_ >= DEFAULT_MAX_POST_RECV / 2) {
    conn_data_event_->post_recvs(ringbuf_event_->buf_, ringbuf_sz_, local_ringbuf_mr_.lkey(), conn_data_event_->rr_garbage_);
    conn_data_event_->rr_garbage_ = 0;
    update_remote_metadata();
  }

  return unread_mlens_;
}

size_t RDMASenderReceiverEvent::recv(msghdr* msg) {
  size_t read_size = ringbuf_event_->read_to_msghdr(msg, unread_mlens_);
  if (read_size == 0) {
    rdma_log(RDMA_WARNING, "RDMASenderReceiverEvent::recv, read_size == 0");
    return 0;
  }

  unread_mlens_ = 0;
  garbage_ += read_size;
  total_recv_sz += read_size;

  if (garbage_ >= ringbuf_sz_ / 2) {
    update_remote_metadata();
    garbage_ = 0;
  }
  return read_size;
}

// this could be optimized.
// caller already checked msg
bool RDMASenderReceiverEvent::send(msghdr* msg, size_t mlen) {
  if (mlen > sendbuf_sz_) {
    rdma_log(RDMA_ERROR, "RDMASenderReceiverEvent::send, mlen > sendbuf size");
    return false;
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

  update_local_metadata(); // update remote_ringbuf_head_ and remote_rr_tail
  size_t used = (remote_ringbuf_sz + remote_ringbuf_tail_ - remote_ringbuf_head_) % remote_ringbuf_sz;
  size_t avail_rr_num = (remote_rr_tail_ - remote_rr_head_ + DEFAULT_MAX_POST_RECV) % DEFAULT_MAX_POST_RECV;
  if (used + mlen >= remote_ringbuf_sz - 1 || avail_rr_num <= 2) return false;

  uint8_t* start = sendbuf_;
  for (size_t iov_idx = 0, nwritten = 0;
       iov_idx < msg->msg_iovlen && nwritten < mlen;
       iov_idx++) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    nwritten += iov_len;
    if (nwritten <= sendbuf_sz_) {
      memcpy(start, iov_base, iov_len);
    } else {
      rdma_log(RDMA_ERROR, "RDMASenderReceiverEvent::send, mlen incorrect");
      return false;
    }
    start += iov_len;
  }

  int n = conn_data_event_->post_send_and_poll_completion(remote_ringbuf_mr_, remote_ringbuf_tail_,
                                                      sendbuf_mr_, 0, mlen, IBV_WR_RDMA_WRITE_WITH_IMM, false);

  remote_rr_head_ = (remote_rr_head_ + n) % DEFAULT_MAX_POST_RECV;
  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
  total_send_sz += mlen;
  return true;
}