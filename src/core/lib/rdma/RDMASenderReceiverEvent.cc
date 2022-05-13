#include <fcntl.h>
#include <infiniband/verbs.h>
#include <thread>
#include "RDMASenderReceiver.h"
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/debug/trace.h"
#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE (1 << 28)
#endif
grpc_core::TraceFlag grpc_trace_sender_receiver(false, "sender_receiver");

RDMASenderReceiverEvent::RDMASenderReceiverEvent()
    : RDMASenderReceiver(new RDMAConn(&RDMANode::GetInstance(), true),
                         new RingBufferEvent(DEFAULT_RINGBUF_SZ)) {
  auto& node = RDMANode::GetInstance();
  conn_metadata_ = new RDMAConn(&node, true);
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
  connected_ = false;
}

RDMASenderReceiverEvent::~RDMASenderReceiverEvent() {
  conn_->poll_send_completion(last_n_post_send_);
  delete conn_metadata_;
  delete ringbuf_;
}

void RDMASenderReceiverEvent::connect(int fd) {
  conn_th_ = std::thread([this, fd] {
    conn_->SyncQP(fd);
    conn_->SyncMR(fd, local_ringbuf_mr_, remote_ringbuf_mr_);
    conn_->SyncMR(fd, local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_);
    conn_metadata_->SyncQP(fd);
    // there are at most DEFAULT_MAX_POST_RECV - 1 outstanding recv requests
    conn_metadata_->post_recvs(DEFAULT_MAX_POST_RECV - 1);
    conn_->post_recvs(DEFAULT_MAX_POST_RECV - 1);
    update_remote_metadata();  // set remote_rr_tail

    barrier(fd);

    connected_ = true;
  });
  conn_th_.detach();
}

void RDMASenderReceiverEvent::update_remote_metadata() {
  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
  reinterpret_cast<size_t*>(metadata_sendbuf_)[1] = conn_->get_rr_tail();
  int n_entries = conn_metadata_->post_send(
      remote_metadata_recvbuf_mr_, 0, metadata_sendbuf_mr_, 0,
      metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE_WITH_IMM);
  conn_metadata_->poll_send_completion(n_entries);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_sender_receiver)) {
    gpr_log(GPR_INFO, "RDMASenderReceiver::update_remote_metadata, %zu, %zu",
            reinterpret_cast<size_t*>(metadata_sendbuf_)[0],
            reinterpret_cast<size_t*>(metadata_sendbuf_)[1]);
  }
}

void RDMASenderReceiverEvent::update_local_metadata() {
  WaitConnect();
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
  remote_rr_tail_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[1];
}

size_t RDMASenderReceiverEvent::check_and_ack_incomings_locked() {
  WaitConnect();
  size_t ret = unread_mlens_;

#ifdef SENDER_RECEIVER_NON_ATOMIC
  if (check_data_) {
    check_data_ = false;
#else
  if (check_data_.exchange(false)) {
#endif
    auto new_mlen = conn_->get_recv_events_locked();
    // GPR_ASSERT(new_mlen > 0);
    if (conn_->post_recvs_lazy()) {
      update_remote_metadata();
    }
#ifdef SENDER_RECEIVER_NON_ATOMIC
    unread_mlens_ += new_mlen;
    ret = unread_mlens_;
#else
    ret = unread_mlens_.fetch_add(new_mlen) + new_mlen;
#endif
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
  }
  return ret;
}

size_t RDMASenderReceiverEvent::recv(msghdr* msg) {
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

// this could be optimized.
// caller already checked msg
// bool RDMASenderReceiverEvent::send(msghdr* msg, size_t mlen) {
//   WaitConnect();
//   if (mlen > ringbuf_->get_max_send_size()) {
//     gpr_log(GPR_ERROR, "RDMASenderReceiverEvent::send, mlen > sendbuf size");
//     return false;
//   }

//   size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

//   if (!is_writable(mlen)) {
//     last_failed_send_size_ = mlen;
//     return false;
//   }
//   conn_->poll_send_completion(last_n_post_send_);

//   {
//     //    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_MEMCPY);
//     uint8_t* start = sendbuf_;
//     for (size_t iov_idx = 0, nwritten = 0;
//          iov_idx < msg->msg_iovlen && nwritten < mlen; iov_idx++) {
//       void* iov_base = msg->msg_iov[iov_idx].iov_base;
//       size_t iov_len = msg->msg_iov[iov_idx].iov_len;
//       nwritten += iov_len;
//       GPR_ASSERT(nwritten <= ringbuf_->get_max_send_size());
//       memcpy(start, iov_base, iov_len);
//       start += iov_len;
//     }
//   }

//   {
//     //    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_IBV);
//     last_n_post_send_ =
//         conn_->post_send(remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_,
//                          0, mlen, IBV_WR_RDMA_WRITE_WITH_IMM);
//   }

//   remote_rr_head_ =
//       (remote_rr_head_ + last_n_post_send_) % DEFAULT_MAX_POST_RECV;
//   remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
//   last_failed_send_size_ = 0;
//   return true;
// }

#define RDMA_MAX_WRITE_IOVEC 1024
bool RDMASenderReceiverEvent::send(msghdr* msg, size_t mlen) {
  WaitConnect();
  if (mlen > ringbuf_->get_max_send_size()) {
    gpr_log(GPR_ERROR, "RDMASenderReceiverEvent::send, mlen > sendbuf size");
    return false;
  }

  size_t remote_ringbuf_sz = remote_ringbuf_mr_.length();

  if (!is_writable(mlen)) {
    last_failed_send_size_ = mlen;
    return false;
  }
  // conn_->poll_send_completion(last_n_post_send_);

  size_t zerocopy_size = 0;
  bool zerocopy_flag = false;
  struct ibv_sge sges[RDMA_MAX_WRITE_IOVEC];
  size_t sge_idx = 0;
  {
    uint8_t *sendbuf_ptr = sendbuf_;
    size_t iov_idx = 0, nwritten = 0;
    init_sge(sges, sendbuf_, 0, sendbuf_mr_.lkey());
    while (iov_idx < msg->msg_iovlen && nwritten < mlen) {
      void* iov_base = msg->msg_iov[iov_idx].iov_base;
      size_t iov_len = msg->msg_iov[iov_idx].iov_len;
      if (zerocopy_sendbuf_contains(iov_base)) {
        zerocopy_flag = true;
        if (sges[sge_idx].length > 0) sge_idx++; // if currecnt sge is not empty, it cannot be a zerocopy sge again, put in next sge
        init_sge(&sges[sge_idx], iov_base, iov_len, zerocopy_sendbuf_mr_.lkey());
        unfinished_zerocopy_send_size_.fetch_sub(iov_len);
        total_zerocopy_send_size += iov_len;
        zerocopy_size += iov_len;
      } else {
        memcpy(sendbuf_ptr, iov_base, iov_len);
        if (sges[sge_idx].lkey == sendbuf_mr_.lkey()) { // last sge in sendbuf
          sges[sge_idx].length += iov_len; // merge in last sge
        } else { // last sge in zerocopy_sendbuf
          init_sge(&sges[++sge_idx], sendbuf_ptr, iov_len, sendbuf_mr_.lkey());
        }
        sendbuf_ptr += iov_len;
      }
      nwritten += iov_len;
      iov_idx++;
    }
  }

  {
    //    GRPCProfiler profiler(GRPC_STATS_TIME_SEND_IBV);
    if (!zerocopy_flag) {
      last_n_post_send_ =
        conn_->post_send(remote_ringbuf_mr_, remote_ringbuf_tail_, sendbuf_mr_,
                         0, mlen, IBV_WR_RDMA_WRITE_WITH_IMM);
    } else {
      last_n_post_send_ = 
          conn_->post_sends(remote_ringbuf_mr_, remote_ringbuf_tail_, 
                            sges, sge_idx + 1, mlen, IBV_WR_RDMA_WRITE_WITH_IMM);
    }
  }
  total_send_size += mlen;
  {
    conn_->poll_send_completion(last_n_post_send_);
  }

  remote_rr_head_ =
      (remote_rr_head_ + last_n_post_send_) % DEFAULT_MAX_POST_RECV;
  remote_ringbuf_tail_ = (remote_ringbuf_tail_ + mlen) % remote_ringbuf_sz;
  last_failed_send_size_ = 0;
  printf("send mlen = %lld, zerocopy_mlen = %lld, total_send_sz = %lld, total_zerocopy_send_sz = %lld, %f\n", 
    mlen, zerocopy_size, total_send_size, total_zerocopy_send_size, double(total_zerocopy_send_size) / total_send_size);
  if (unfinished_zerocopy_send_size_.load() == 0) {
    last_zerocopy_send_finished_.store(true);
  }
  return true;
}