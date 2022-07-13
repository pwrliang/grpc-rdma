#include <fcntl.h>
#include <infiniband/verbs.h>
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"
grpc_core::TraceFlag grpc_rdma_sr_event_trace(false, "rdma_sr_event");

RDMASenderReceiverEvent::RDMASenderReceiverEvent(bool server)
    : RDMASenderReceiver(
          new RDMAConn(&RDMANode::GetInstance(), true),
          new RDMAConn(&RDMANode::GetInstance(), true),
          new RingBufferEvent(RDMAConfig::GetInstance().get_ring_buffer_size()),
          server) {
  auto& node = RDMANode::GetInstance();
  auto pd = node.get_pd();
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_->get_buf(),
                                  ringbuf_->get_capacity())) {
    gpr_log(
        GPR_ERROR,
        "RDMASenderReceiverEvent::RDMASenderReceiverEvent, failed to local_reg "
        "local_ringbuf_mr");
    exit(-1);
  }

  check_data_ = false;
  check_metadata_ = false;
  last_failed_send_size_ = 0;
  last_n_post_send_ = 0;
}

void RDMASenderReceiverEvent::Shutdown() {
  update_local_metadata();
  if (remote_exit_ == 1) return;
  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = conn_data_->get_rr_tail();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[2] = 1;
  int n_entries = conn_metadata_->post_send(
      remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
      metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE);
  conn_metadata_->poll_send_completion(n_entries);
}

RDMASenderReceiverEvent::~RDMASenderReceiverEvent() {
  // conn_->poll_send_completion(last_n_post_send_);
  delete conn_metadata_;
  delete ringbuf_;
}

void RDMASenderReceiverEvent::connect(int fd) {
  fd_ = fd;
  conn_data_->SyncQP(fd);
  conn_data_->SyncMR(fd, local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_data_->SyncMR(fd, local_metadata_recvbuf_mr_,
                     remote_metadata_recvbuf_mr_);
  conn_metadata_->SyncQP(fd);
  barrier(fd);
  // there are at most DEFAULT_MAX_POST_RECV - 1 outstanding recv requests
  conn_metadata_->post_recvs(DEFAULT_MAX_POST_RECV - 1);
  conn_data_->post_recvs(DEFAULT_MAX_POST_RECV - 1);
  update_remote_metadata();  // set remote_rr_tail
}

void RDMASenderReceiverEvent::update_remote_metadata() {
  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = conn_data_->get_rr_tail();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[2] = 0;
  int n_entries = conn_metadata_->post_send(
      remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
      metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE_WITH_IMM);
  int ret = conn_metadata_->poll_send_completion(n_entries);

  if (ret != 0) {
    gpr_log(GPR_ERROR,
            "poll_send_completion failed, code: %d "
            "fd = %d, remote_ringbuf_tail = "
            "%zu, post_num = %d",
            ret, fd_, remote_ringbuf_tail_, n_outstanding_send_);
    abort();
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
    gpr_log(GPR_INFO, "update_remote_metadata, head: %zu, exit: %zu",
            reinterpret_cast<size_t*>(metadata_sendbuf_)[0],
            reinterpret_cast<size_t*>(metadata_sendbuf_)[1]);
  }
}

void RDMASenderReceiverEvent::update_local_metadata() {
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
  remote_rr_tail_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
  remote_exit_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[2];
}

size_t RDMASenderReceiverEvent::check_and_ack_incomings_locked() {
  size_t ret = unread_mlens_;

#ifdef SENDER_RECEIVER_NON_ATOMIC
  if (check_data_) {
    check_data_ = false;
#else
  if (check_data_.exchange(false)) {
#endif
    auto new_mlen = conn_data_->get_recv_events_locked();
    // GPR_ASSERT(new_mlen > 0);
    if (conn_data_->post_recvs_lazy()) {
      update_remote_metadata();
    }
    ret = unread_mlens_.fetch_add(new_mlen) + new_mlen;
  }

#ifdef SENDER_RECEIVER_NON_ATOMIC
  if (check_metadata_) {
    check_metadata_ = false;
#else
  if (check_metadata_.exchange(false)) {
#endif
    // get_cq_event and poll recv completion
    conn_metadata_->get_recv_events_locked();
    size_t finsihed_rr =
        conn_metadata_->get_rr_garbage();  // incrased by poll_recv_completion
    conn_metadata_->post_recvs(finsihed_rr);
    conn_metadata_->set_rr_garbage(0);
    update_local_metadata();

    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
      gpr_log(GPR_INFO, "check_and_ack_incomings_locked, unread_mlens: %zu",
              ret);
    }
  }

  return ret;
}

size_t RDMASenderReceiverEvent::recv(msghdr* msg) {
  ContentAssertion cass(read_counter_);
  size_t expected_len = unread_mlens_;
  bool should_recycle = ringbuf_->read_to_msghdr(msg, expected_len);
  GPR_ASSERT(expected_len > 0);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
    gpr_log(GPR_INFO, "recv, unread_mlens: %zu", expected_len);
  }

  unread_mlens_ -= expected_len;

  if (should_recycle) {
    update_remote_metadata();
  }
  return expected_len;
}

bool RDMASenderReceiverEvent::send(msghdr* msg, size_t mlen) {
  ContentAssertion cass(write_counter_);
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

  if (mlen == 0 || mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "Invalid mlen, expected (0, %zu] actually size: %zu",
            ringbuf_->get_max_send_size(), mlen);
    abort();
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
    gpr_log(GPR_INFO, "send, mlen: %zu", mlen);
  }

  update_local_metadata();
  if (!is_writable(mlen)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
      gpr_log(GPR_INFO, "ring buffer is full, with mlen: %zu", mlen);
    }
    last_failed_send_size_ = mlen;
    return false;
  }

  bool zerocopy = false;
  struct ibv_sge sges[RDMA_MAX_WRITE_IOVEC];
  size_t sge_idx = 0;

  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_MEMCPY);
    uint8_t* sendbuf_ptr = sendbuf_;
    size_t iov_idx = 0, nwritten = 0;

    init_sge(sges, sendbuf_, 0, sendbuf_mr_.lkey());
    while (iov_idx < msg->msg_iovlen && nwritten < mlen) {
      void* iov_base = msg->msg_iov[iov_idx].iov_base;
      size_t iov_len = msg->msg_iov[iov_idx].iov_len;
      if (zerocopy_sendbuf_contains(iov_base)) {
        zerocopy = true;
        if (sges[sge_idx].length > 0) {
          sge_idx++;  // if currecnt sge is not empty, it cannot be a zerocopy
                      // sge again, put in next sge
        }
        init_sge(&sges[sge_idx], iov_base, iov_len,
                 zerocopy_sendbuf_mr_.lkey());
        unfinished_zerocopy_send_size_.fetch_sub(iov_len);
        total_zerocopy_send_size += iov_len;
      } else {
        memcpy(sendbuf_ptr, iov_base, iov_len);
        if (sges[sge_idx].lkey == sendbuf_mr_.lkey()) {  // last sge in sendbuf
          sges[sge_idx].length += iov_len;               // merge in last sge
        } else {  // last sge in zerocopy_sendbuf
          init_sge(&sges[++sge_idx], sendbuf_ptr, iov_len, sendbuf_mr_.lkey());
        }
        sendbuf_ptr += iov_len;
      }
      nwritten += iov_len;
      iov_idx++;
    }
  }

  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_IBV);
    if (!zerocopy) {
      last_n_post_send_ = conn_data_->post_send(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, 0, mlen,
          IBV_WR_RDMA_WRITE_WITH_IMM);
    } else {
      last_n_post_send_ =
          conn_data_->post_sends(remote_ringbuf_mr_, remote_ringbuf_tail_, sges,
                                 sge_idx + 1, mlen, IBV_WR_RDMA_WRITE_WITH_IMM);
    }
    int ret = conn_data_->poll_send_completion(last_n_post_send_);
    if (ret != 0) {
      gpr_log(GPR_ERROR,
              "poll_send_completion failed, code: %d "
              "fd = %d, mlen = %zu, remote_ringbuf_tail = "
              "%zu, ringbuf_sz = %zu, post_num = %d",
              ret, fd_, mlen, remote_ringbuf_tail_, remote_ringbuf_sz,
              last_n_post_send_);
      abort();
    }
    // last_n_post_send_ = 0;
  }
  total_send_size += mlen;
  remote_rr_head_ =
      (remote_rr_head_ + last_n_post_send_) % DEFAULT_MAX_POST_RECV;
  last_n_post_send_ = 0;

  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
  last_failed_send_size_ = 0;
  if (unfinished_zerocopy_send_size_ == 0) {
    last_zerocopy_send_finished_ = true;
  }
  return true;
}
