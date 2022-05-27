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

RDMASenderReceiverAdaptive::RDMASenderReceiverAdaptive()
    : RDMASenderReceiver(new RDMAConn(&RDMANode::GetInstance(), true),
                         new RingBufferAdaptive(DEFAULT_RINGBUF_SZ)) {
  auto& node = RDMANode::GetInstance();
  auto pd = node.get_pd();

  conn_metadata_ = new RDMAConn(&node, false);
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_->get_buf(),
                                  ringbuf_->get_capacity())) {
    gpr_log(GPR_ERROR,
            "RDMASenderReceiverAdaptive::RDMASenderReceiverAdaptive, failed to "
            "local_reg "
            "local_ringbuf_mr");
    exit(-1);
  }

  check_data_ = false;
  last_failed_send_size_ = 0;
  last_n_post_send_ = 0;
  connected_ = false;
  wakeup_fd_ = eventfd(0, EFD_NONBLOCK);
  last_mode_ = RDMASenderReceiverMode::kNone;
  mode_ = RDMASenderReceiverMode::kEvent;
  peer_mode_ = RDMASenderReceiverMode::kEvent;
}

void RDMASenderReceiverAdaptive::Shutdown() {}

RDMASenderReceiverAdaptive::~RDMASenderReceiverAdaptive() {
  // conn_->poll_send_completion(last_n_post_send_);
  RDMAMonitor::GetInstance().Unregister(this);
  close(wakeup_fd_);
  delete conn_metadata_;
  delete ringbuf_;
}

void RDMASenderReceiverAdaptive::connect(int fd) {
  fd_ = fd;
  conn_th_ = std::thread([this, fd] {
    conn_->SyncQP(fd);
    conn_->SyncMR(fd, local_ringbuf_mr_, remote_ringbuf_mr_);
    conn_->SyncMR(fd, local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_);
    conn_metadata_->SyncQP(fd);
    // there are at most DEFAULT_MAX_POST_RECV - 1 outstanding recv requests
    conn_->post_recvs(DEFAULT_MAX_POST_RECV - 1);
    update_remote_metadata();  // set remote_rr_tail

    barrier(fd);

    connected_ = true;
    RDMAMonitor::GetInstance().Register(this);
  });
  conn_th_.detach();
}

void RDMASenderReceiverAdaptive::update_remote_metadata() {
  auto mode = mode_;

  GPR_ASSERT(mode != RDMASenderReceiverMode::kNone);
  // Infer next state
  if (mode == RDMASenderReceiverMode::kMigrating) {
    if (last_mode_ == RDMASenderReceiverMode::kBP) {
      mode = RDMASenderReceiverMode::kEvent;
    } else if (last_mode_ == RDMASenderReceiverMode::kEvent) {
      mode = RDMASenderReceiverMode::kBP;
    } else {
      GPR_ASSERT(false);
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_sender_receiver_adaptive)) {
    gpr_log(GPR_INFO, "Set mode to: %s", rdma_mode_to_string(mode));
  }

  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = conn_->get_rr_tail();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[2] = 0;
  reinterpret_cast<size_t*>(metadata_sendbuf_)[3] = static_cast<size_t>(mode);

  int n_entries = conn_metadata_->post_send(
      remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
      metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
  conn_metadata_->poll_send_completion(n_entries);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_sender_receiver_adaptive)) {
    gpr_log(GPR_INFO, "RDMASenderReceiver::update_remote_metadata, %zu, %zu",
            reinterpret_cast<size_t*>(metadata_sendbuf_)[0],
            reinterpret_cast<size_t*>(metadata_sendbuf_)[1]);
  }
}

void RDMASenderReceiverAdaptive::update_local_metadata() {
  WaitConnect();
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
  remote_rr_tail_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
  remote_exit_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[2];
  auto mode = static_cast<RDMASenderReceiverMode>(
      reinterpret_cast<size_t*>(metadata_recvbuf_)[3]);
  peer_mode_ = mode;
}

bool RDMASenderReceiverAdaptive::check_incoming() const {
  WaitConnect();
  RDMASenderReceiverMode peer_mode;
  return dynamic_cast<RingBufferAdaptive*>(ringbuf_)->check_mlen(peer_mode) > 0;
}

size_t RDMASenderReceiverAdaptive::check_and_ack_incomings_locked(
    bool enable_switch) {
  WaitConnect();
  RDMASenderReceiverMode peer_mode;
  // Look at first send
  if (dynamic_cast<RingBufferAdaptive*>(ringbuf_)->check_mlen(peer_mode) == 0) {
    return unread_mlens_;
  }

  unread_mlens_ = dynamic_cast<RingBufferAdaptive*>(ringbuf_)->check_mlens();

  auto consume_events = [&]() {
#ifdef SENDER_RECEIVER_NON_ATOMIC
    if (check_data_) {
      check_data_ = false;
#else
    if (check_data_.exchange(false)) {
#endif
      conn_->get_recv_events_locked();  // return value has hdr and tail
      if (conn_->post_recvs_lazy()) {
        update_remote_metadata();
      }
    }
  };

  if (peer_mode == RDMASenderReceiverMode::kEvent) {
    // N.B. is check_data_ is false and sender sends me event,
    // means RDMAMonitor wakes me up when migrating from BP to Event, we
    // do nothing otherwise a RDMA event will be consumed
    if (!check_data_) {
      return unread_mlens_ = 0;
    }
    consume_events();
  }

  // Sender knows the new mode, migrating is done
  if (mode_ == RDMASenderReceiverMode::kMigrating && last_mode_ != peer_mode) {
    mode_ = peer_mode;
  }
  if (enable_switch) {
    RDMAMonitor::GetInstance().Report(this);
  }
  return unread_mlens_;
}

size_t RDMASenderReceiverAdaptive::recv(msghdr* msg) {
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

bool RDMASenderReceiverAdaptive::send(msghdr* msg, size_t mlen) {
  WaitConnect();

  if (mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "RDMASenderReceiverAdaptive::send, mlen > sendbuf size");
    return false;
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

  if (!is_writable(mlen)) {
    last_failed_send_size_ = mlen;
    return false;
  }

  auto copy_to_sendbuf = [&](uint8_t* start) {
    // 8 bytes to represent mode, 56 bytes represent payload len
    uint64_t hdr = (static_cast<uint64_t>(peer_mode_) << 56) | mlen;

    *reinterpret_cast<uint64_t*>(start) = hdr;  // 8 bytes header
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

  switch (peer_mode_) {
    case RDMASenderReceiverMode::kEvent: {
      size_t data_len = copy_to_sendbuf(sendbuf_) - sendbuf_;

      last_n_post_send_ = conn_->post_send(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, 0, data_len,
          IBV_WR_RDMA_WRITE_WITH_IMM);
      conn_->poll_send_completion(last_n_post_send_);

      remote_rr_head_ =
          (remote_rr_head_ + last_n_post_send_) % DEFAULT_MAX_POST_RECV;
      remote_ringbuf_tail_ =
          (remote_ringbuf_tail_ + data_len) % remote_ringbuf_sz;
      last_failed_send_size_ = 0;
      break;
    }
    case RDMASenderReceiverMode::kBP: {
      size_t data_len = copy_to_sendbuf(sendbuf_) - sendbuf_;

      last_n_post_send_ =
          conn_->post_send(remote_ringbuf_mr_, remote_ringbuf_tail_,
                           sendbuf_mr_, 0, data_len, IBV_WR_RDMA_WRITE);
      conn_->poll_send_completion(last_n_post_send_);

      remote_ringbuf_tail_ =
          (remote_ringbuf_tail_ + data_len) % remote_ringbuf_sz;
      last_failed_send_size_ = 0;
      break;
    }
    default:
      GPR_ASSERT(false);
  }

  return true;
}
