#include <fcntl.h>
#include <infiniband/verbs.h>
#include <sys/eventfd.h>
#include <thread>
#include "RDMASenderReceiver.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/rdma/RDMAMonitor.h"
#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE (1 << 28)
#endif
grpc_core::TraceFlag grpc_trace_sender_receiver_adaptive(false,
                                                         "sender_receiver");

RDMASenderReceiverBPEV::RDMASenderReceiverBPEV()
    : RDMASenderReceiver(new RDMAConn(&RDMANode::GetInstance(), true),
                         new RingBufferBP(DEFAULT_RINGBUF_SZ)) {
  auto& node = RDMANode::GetInstance();
  auto pd = node.get_pd();

  if (local_ringbuf_mr_.local_reg(pd, ringbuf_->get_buf(),
                                  ringbuf_->get_capacity())) {
    gpr_log(GPR_ERROR,
            "RDMASenderReceiverBPEV::RDMASenderReceiverBPEV, failed to "
            "local_reg "
            "local_ringbuf_mr");
    exit(-1);
  }

  last_failed_send_size_ = 0;
  last_n_post_send_ = 0;
  connected_ = false;
  wakeup_fd_ = eventfd(0, EFD_NONBLOCK);
}

void RDMASenderReceiverBPEV::Shutdown() {}

RDMASenderReceiverBPEV::~RDMASenderReceiverBPEV() {
  // conn_->poll_send_completion(last_n_post_send_);
  RDMAMonitor::GetInstance().Unregister(this);
  close(wakeup_fd_);
  delete ringbuf_;
}

void RDMASenderReceiverBPEV::connect(int fd) {
  fd_ = fd;
  conn_th_ = std::thread([this, fd] {
    conn_->SyncQP(fd);
    conn_->SyncMR(fd, local_ringbuf_mr_, remote_ringbuf_mr_);
    conn_->SyncMR(fd, local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_);
    barrier(fd);

    connected_ = true;
    RDMAMonitor::GetInstance().Register(this);
  });
  conn_th_.detach();
}

void RDMASenderReceiverBPEV::update_remote_metadata() {
  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = 0;
  int n_entries =
      conn_->post_send(remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
                       metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
  conn_->poll_send_completion(n_entries);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_sender_receiver_adaptive)) {
    gpr_log(GPR_INFO, "RDMASenderReceiver::update_remote_metadata, %zu, %zu",
            reinterpret_cast<size_t*>(metadata_sendbuf_)[0],
            reinterpret_cast<size_t*>(metadata_sendbuf_)[1]);
  }
}

void RDMASenderReceiverBPEV::update_local_metadata() {
  WaitConnect();
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
  remote_exit_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
}

bool RDMASenderReceiverBPEV::check_incoming() const {
  WaitConnect();
  return dynamic_cast<RingBufferBP*>(ringbuf_)->check_mlen() > 0;
}

size_t RDMASenderReceiverBPEV::check_and_ack_incomings_locked() {
  WaitConnect();
  // Look at first send
  if (dynamic_cast<RingBufferBP*>(ringbuf_)->check_mlen() == 0) {
    return unread_mlens_;
  }

  return unread_mlens_ = dynamic_cast<RingBufferBP*>(ringbuf_)->check_mlens();
}

size_t RDMASenderReceiverBPEV::recv(msghdr* msg) {
  WaitConnect();
  size_t expected_len = unread_mlens_;
  bool should_recycle = ringbuf_->read_to_msghdr(msg, expected_len);
  GPR_ASSERT(expected_len > 0);

  unread_mlens_ -= expected_len;

  if (should_recycle) {
    update_remote_metadata();
  }
  return expected_len;
}

bool RDMASenderReceiverBPEV::send(msghdr* msg, size_t mlen) {
  WaitConnect();

  if (mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "RDMASenderReceiverBPEV::send, mlen > sendbuf size");
    return false;
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

  if (!is_writable(mlen)) {
    last_failed_send_size_ = mlen;
    return false;
  }

  auto copy_to_sendbuf = [&](uint8_t* start) {
    *reinterpret_cast<uint64_t*>(start) = mlen;  // 8 bytes header
    start += sizeof(uint64_t);

    // copy payload
    for (size_t iov_idx = 0, nwritten = 0;
         iov_idx < msg->msg_iovlen && nwritten < mlen; iov_idx++) {
      void* iov_base = msg->msg_iov[iov_idx].iov_base;
      size_t iov_len = msg->msg_iov[iov_idx].iov_len;
      nwritten += iov_len;
      GPR_ASSERT(nwritten <= ringbuf_->get_max_send_size());
      memcpy(start, iov_base, iov_len);
      start += iov_len;
    }

    *start = 1;  // 1 byte tail
    start++;

    return start;
  };

  size_t data_len = copy_to_sendbuf(sendbuf_) - sendbuf_;

  last_n_post_send_ =
      conn_->post_send(remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, 0,
                       data_len, IBV_WR_RDMA_WRITE);
  conn_->poll_send_completion(last_n_post_send_);

  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + data_len) % remote_ringbuf_sz;
  last_failed_send_size_ = 0;

  return true;
}
