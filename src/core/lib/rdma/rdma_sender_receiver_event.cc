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
  ContentAssertion cass(write_content_counter_);
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

  pollLastSendCompletion();

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

  int n_post_send;
  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_POST);

    if (!zerocopy) {
      n_post_send = conn_data_->PostSendRequest(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, 0, mlen,
          IBV_WR_RDMA_WRITE_WITH_IMM);
    } else {
      n_post_send = conn_data_->PostSendRequests(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sges, sge_idx + 1, mlen,
          IBV_WR_RDMA_WRITE_WITH_IMM);
    }
    n_outstanding_send_ += n_post_send;
  }

  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
  remote_rr_head_ = (remote_rr_head_ + n_post_send) % DEFAULT_MAX_POST_RECV;
  last_failed_send_size_ = 0;
  if (unfinished_zerocopy_send_size_ == 0) {
    last_zerocopy_send_finished_ = true;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_debug_trace)) {
    total_sent_ += nwritten;
    printf("zerocopy send = %lld, total send = %lld, ratio = %.4lf\n",
           total_zerocopy_send_size, total_sent_.load(),
           double(total_zerocopy_send_size) / total_sent_.load());
  }
  if (sz != nullptr) {
    *sz = nwritten;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_trace)) {
    gpr_log(GPR_INFO, "send, mlen: %zu", mlen);
  }

  return 0;
}

int RDMASenderReceiverEvent::SendChunk(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(write_content_counter_);
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

  if (sz != nullptr) {
    *sz = -1;
  }

  if (status_ != Status::kConnected) {
    return EPIPE;
  }
  auto ringbuf_head = get_remote_ringbuf_head();  // get head snapshot
  auto get_avail_rr = [this]() {
    size_t remote_rr_tail = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
    return (remote_rr_tail - remote_rr_head_ + DEFAULT_MAX_POST_RECV) %
           DEFAULT_MAX_POST_RECV;
  };
  auto get_avail_space = [this, remote_ringbuf_sz, ringbuf_head]() {
    size_t used_space =
        (remote_ringbuf_sz + remote_ringbuf_tail_ - ringbuf_head) %
        remote_ringbuf_sz;
    return remote_ringbuf_sz - 8 - used_space;
  };

  size_t total_size = 0;
  size_t written_size = 0;
  size_t offset = 0;

  for (size_t iov_idx = 0; iov_idx < msg->msg_iovlen; iov_idx++) {
    total_size += msg->msg_iov[iov_idx].iov_len;
  }

  size_t chunk_size = RDMAConfig::GetInstance().get_send_chunk_size();

  for (size_t iov_idx = 0; iov_idx < msg->msg_iovlen; iov_idx++) {
    void* iov_base = msg->msg_iov[iov_idx].iov_base;
    size_t iov_len = msg->msg_iov[iov_idx].iov_len;
    size_t writable_size = std::min(get_max_send_size() - written_size,
                                    std::min(iov_len, get_avail_space()));
    size_t rest_size = writable_size;

    // reserve 5 recv requests
    while (rest_size > 0 && get_avail_rr() > 5) {
      size_t curr_writable_size = std::min(chunk_size, rest_size);

      memcpy(sendbuf_ + offset, iov_base, curr_writable_size);
      GPR_ASSERT(offset < get_max_send_size());

      int n_posted = conn_data_->PostSendRequest(
          remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_, offset,
          curr_writable_size, IBV_WR_RDMA_WRITE_WITH_IMM);
      n_outstanding_send_ += n_posted;
      remote_rr_head_ = (remote_rr_head_ + n_posted) % DEFAULT_MAX_POST_RECV;
      remote_ringbuf_tail_ =
          (remote_ringbuf_tail_ + curr_writable_size) % remote_ringbuf_sz;
      written_size += curr_writable_size;
      offset += curr_writable_size;
      rest_size -= curr_writable_size;
    }

    // rr drained
    if (rest_size > 0) {
      break;
    }
  }
  last_failed_send_size_ = total_size - written_size;

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_debug_trace)) {
    total_sent_ += written_size;
  }

  if (sz != nullptr) {
    if (written_size == 0) {
      return EAGAIN;
    }
    *sz = written_size;
  }

  return 0;
}

int RDMASenderReceiverEvent::Recv(msghdr* msg, ssize_t* sz) {
  ContentAssertion cass(read_content_conter_);
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
    if (mlens == 0) {
      *sz = -1;
      return EAGAIN;
    }
    *sz = mlens;
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_event_debug_trace)) {
    total_recv_ += mlens;
  }
  return 0;
}
