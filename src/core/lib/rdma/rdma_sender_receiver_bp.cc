#include <sstream>
#include "grpc/impl/codegen/log.h"
#include "include/grpcpp/stats_time.h"

#include "src/core/lib/rdma/rdma_conn.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"

grpc_core::TraceFlag grpc_rdma_sr_bp_trace(false, "rdma_sr_bp");

RDMASenderReceiverBP::RDMASenderReceiverBP(bool server)
    : RDMASenderReceiver(
          new RDMAConn(&RDMANode::GetInstance()),
          new RDMAConn(&RDMANode::GetInstance()),
          new RingBufferBP(RDMAConfig::GetInstance().get_ring_buffer_size()),
          server) {
  auto pd = RDMANode::GetInstance().get_pd();
  auto& node = RDMANode::GetInstance();
  if (local_ringbuf_mr_.local_reg(pd, ringbuf_->get_buf(),
                                  ringbuf_->get_capacity())) {
    gpr_log(GPR_ERROR, "failed to local_reg local_ringbuf_mr");
    exit(-1);
  }
}

RDMASenderReceiverBP::~RDMASenderReceiverBP() {
  delete conn_data_;
  delete ringbuf_;
}

void RDMASenderReceiverBP::connect(int fd) {
  fd_ = fd;
  conn_data_->SyncQP(fd);
  conn_data_->SyncMR(fd, local_ringbuf_mr_, remote_ringbuf_mr_);
  conn_data_->SyncMR(fd, local_metadata_recvbuf_mr_,
                     remote_metadata_recvbuf_mr_);
  conn_metadata_->SyncQP(fd);
  barrier(fd);
}

bool RDMASenderReceiverBP::check_incoming() const {
  return dynamic_cast<RingBufferBP*>(ringbuf_)->check_mlen() > 0;
}

size_t RDMASenderReceiverBP::check_and_ack_incomings_locked() {
  return unread_mlens_ = dynamic_cast<RingBufferBP*>(ringbuf_)->check_mlens();
}

size_t RDMASenderReceiverBP::recv(msghdr* msg) {
  ContentAssertion cass(read_counter_);
  // the actual read size is unread_data_size
  size_t mlens = unread_mlens_;
  GPR_ASSERT(mlens > 0);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    gpr_log(GPR_INFO, "recv, unread_mlens: %zu", mlens);
  }

  // since we may read more data than unread_mlens_, mlens will be updated to
  // the real mlens we have read
  bool should_recycle = ringbuf_->read_to_msghdr(msg, mlens);
  unread_mlens_ -= mlens;
  if (should_recycle) {
    update_remote_metadata();
  }

  return mlens;
}

bool RDMASenderReceiverBP::send(msghdr* msg, size_t mlen) {
  ContentAssertion cass(write_counter_);
  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();
  size_t len = mlen + sizeof(size_t) + 1;

  if (mlen == 0 || mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "Invalid mlen, expected (0, %zu] actually size: %zu",
            ringbuf_->get_max_send_size(), mlen);
    abort();
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
    gpr_log(GPR_INFO, "send, mlen: %zu", mlen);
  }

  update_local_metadata();
  if (!is_writable(mlen)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_sr_bp_trace)) {
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
    uint8_t* sendbuf_ptr = sendbuf_ + sizeof(size_t);
    size_t iov_idx = 0, nwritten = 0;

    *reinterpret_cast<size_t*>(sendbuf_) = mlen;
    init_sge(sges, sendbuf_, sizeof(size_t), sendbuf_mr_.lkey());

    while (iov_idx < msg->msg_iovlen && nwritten < mlen) {
      void* iov_base = msg->msg_iov[iov_idx].iov_base;
      size_t iov_len = msg->msg_iov[iov_idx].iov_len;
      if (zerocopy_sendbuf_contains(iov_base)) {
        zerocopy = true;
        init_sge(&sges[++sge_idx], iov_base, iov_len,
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
    *sendbuf_ptr = 1;
    if (sges[sge_idx].lkey == sendbuf_mr_.lkey()) {
      sges[sge_idx].length += 1;
    } else {
      init_sge(&sges[++sge_idx], sendbuf_ptr, 1, sendbuf_mr_.lkey());
    }
  }

  {
    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_POST);
    if (zerocopy) {
      n_outstanding_send_ =
          conn_data_->post_sends(remote_ringbuf_mr_, remote_ringbuf_tail_, sges,
                                 sge_idx + 1, len, IBV_WR_RDMA_WRITE);
    } else {
      n_outstanding_send_ =
          conn_data_->post_send(remote_ringbuf_mr_, remote_ringbuf_tail_,
                                sendbuf_mr_, 0, len, IBV_WR_RDMA_WRITE);
    }
    int ret = conn_data_->poll_send_completion(n_outstanding_send_);
    if (ret != 0) {
      gpr_log(GPR_ERROR,
              "poll_send_completion failed, code: %d "
              "fd = %d, mlen = %zu, remote_ringbuf_tail = "
              "%zu, ringbuf_sz = %zu, post_num = %d",
              ret, fd_, mlen, remote_ringbuf_tail_, remote_ringbuf_sz,
              n_outstanding_send_);
      abort();
    }
    n_outstanding_send_ = 0;
  }

  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + len) % remote_ringbuf_sz;
  total_send_size += mlen;
  last_failed_send_size_ = 0;
  if (unfinished_zerocopy_send_size_ == 0) {
    last_zerocopy_send_finished_ = true;
  }

  return true;
}
