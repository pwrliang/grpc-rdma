#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"

grpc_core::TraceFlag grpc_rdma_sr_event_trace(false, "rdma_sr_event");
grpc_core::TraceFlag grpc_rdma_sr_event_debug_trace(false,
                                                    "rdma_sr_event_debug");

RDMASenderReceiverEvent::RDMASenderReceiverEvent(int fd, bool server)
    : RDMASenderReceiver(
          new RDMAConn(fd, &RDMANode::GetInstance(), true),
          new RingBufferEvent(RDMAConfig::GetInstance().get_ring_buffer_size()),
          new RDMAConn(fd, &RDMANode::GetInstance(), true), server),
      data_ready_(false),
      metadata_ready_(false) {
  auto pd = RDMANode::GetInstance().get_pd();

  if (local_ringbuf_mr_.RegisterLocal(pd, ringbuf_->get_buf(),
                                      ringbuf_->get_capacity())) {
    gpr_log(GPR_ERROR,
            "RDMASenderReceiverEvent::RDMASenderReceiverEvent, failed to "
            "RegisterLocal "
            "local_ringbuf_mr");
    abort();
  }
}

RDMASenderReceiverEvent::~RDMASenderReceiverEvent() {
  delete conn_data_;
  delete conn_metadata_;
  delete ringbuf_;
}

void RDMASenderReceiverEvent::Init() {
  conn_data_->SyncMR(local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_data_->SyncMR(local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_);
  conn_data_->SyncQP();
  conn_metadata_->SyncQP();

  // there are at most DEFAULT_MAX_POST_RECV - 1 outstanding recv requests
  conn_metadata_->PostRecvRequests(DEFAULT_MAX_POST_RECV - 1);
  conn_data_->PostRecvRequests(DEFAULT_MAX_POST_RECV - 1);
  conn_metadata_->Sync();  // Make sure both side posted recv requests
  conn_data_->Sync();
  int r = updateRemoteMetadata();  // set remote_rr_tail
  GPR_ASSERT(r == 0);
  status_ = Status::kConnected;
}

size_t RDMASenderReceiverEvent::MarkMessageLength() {
  size_t mlens = unread_mlens_;

  if (data_ready_.exchange(false)) {
    size_t mlen = conn_data_->GetRecvEvents();

    if (conn_data_->PostRecvRequestsLazy()) {
      int err = updateRemoteMetadata();

      // todo: handle error
      GPR_ASSERT(err == 0);
    }
    mlens = unread_mlens_.fetch_add(mlen) + mlen;
  }

  if (metadata_ready_.exchange(false)) {
    size_t finsihed_rr =
        conn_metadata_->get_rr_garbage();  // incrased by poll_recv_completion
    conn_metadata_->PostRecvRequests(finsihed_rr);
    conn_metadata_->set_rr_garbage(0);
    conn_metadata_->GetRecvEvents();
  }
  return mlens;
}

int RDMASenderReceiverEvent::Send(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(write_counter_);
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t mlen = 0;

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ != Status::kConnected) {
    return EPIPE;
  }

  for (int i = 0; i < msg->msg_iovlen; i++) {
    mlen += msg->msg_iov[i].iov_len;
  }

  if (mlen == 0 || mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "Invalid mlen, expected (0, %zu] actually size: %zu",
            ringbuf_->get_max_send_size(), mlen);
    return EINVAL;
  }

  if (!isWritable(mlen)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
      gpr_log(GPR_INFO, "ring buffer is full, with mlen: %zu", mlen);
    }
    last_failed_send_size_ = mlen;
    return EAGAIN;
  }

  bool zerocopy = false;
  struct ibv_sge sges[RDMA_MAX_WRITE_IOVEC];
  size_t sge_idx = 0;

  uint8_t* sendbuf_ptr = sendbuf_;
  size_t iov_idx = 0, nwritten = 0;

  init_sge(sges, sendbuf_, 0, sendbuf_mr_.lkey());
  while (iov_idx < msg->msg_iovlen && nwritten < mlen) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    if (ZerocopySendbufContains(iov_base)) {
      zerocopy = true;
      if (sges[sge_idx].length > 0) {
        sge_idx++;  // if currecnt sge is not empty, it cannot be a zerocopy
                    // sge again, put in next sge
      }
      init_sge(&sges[sge_idx], iov_base, iov_len, zerocopy_sendbuf_mr_.lkey());
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

  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_IBV);

    if (!zerocopy) {
      n_outstanding_send_ = conn_data_->PostSendRequest(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, 0, mlen,
          IBV_WR_RDMA_WRITE_WITH_IMM);
    } else {
      n_outstanding_send_ = conn_data_->PostSendRequests(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sges, sge_idx + 1, mlen,
          IBV_WR_RDMA_WRITE_WITH_IMM);
    }
    int ret = conn_data_->PollSendCompletion(n_outstanding_send_);

    if (ret != 0) {
      gpr_log(GPR_ERROR,
              "PollSendCompletion failed, code: %d "
              "mlen = %zu, remote_ringbuf_tail = "
              "%zu, ringbuf_sz = %zu, post_num = %d",
              ret, mlen, remote_ringbuf_tail_, remote_ringbuf_sz,
              n_outstanding_send_);
      return EPIPE;
    }
    // N.B. n_outstanding_send_ = 0;
  }

  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
  remote_rr_head_ =
      (remote_rr_head_ + n_outstanding_send_) % DEFAULT_MAX_POST_RECV;
  n_outstanding_send_ = 0;
  last_failed_send_size_ = 0;
  if (unfinished_zerocopy_send_size_ == 0) {
    last_zerocopy_send_finished_ = true;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_debug_trace)) {
    total_sent_ += nwritten;
  }
  if (sz != nullptr) {
    *sz = nwritten;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
    gpr_log(GPR_INFO, "send, mlen: %zu", mlen);
  }

  return 0;
}

int RDMASenderReceiverEvent::Recv(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(read_counter_);
  size_t mlens = unread_mlens_;

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ == Status::kNew || status_ == Status::kDisconnected) {
    return ENOTCONN;
  }

  if (mlens == 0) {
    if (sz != nullptr && status_ == Status::kShutdown) {
      *sz = 0;
    }
    return EAGAIN;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
    gpr_log(GPR_INFO, "recv, unread_mlens: %zu", mlens);
  }

  bool should_recycle = ringbuf_->Read(msg, mlens);

  if (should_recycle && status_ != Status::kShutdown) {
    int r = updateRemoteMetadata();
    // N.B. IsPeerAlive calls read, should put it on the rhs to reduce overhead
    if (r != 0 && conn_metadata_->IsPeerAlive()) {
      return r;
    }
  }

  unread_mlens_ -= mlens;

  if (sz != nullptr) {
    *sz = mlens;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_debug_trace)) {
    total_recv_ += mlens;
  }
  return 0;
}
