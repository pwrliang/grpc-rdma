#include <infiniband/verbs.h>
#include "RDMAConn.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include "fcntl.h"

// -----< RDMAConnEvent >-----
RDMAConnEvent::RDMAConnEvent(int fd, RDMANode* node, ibv_comp_channel* recv_channel) {
  fd_ = fd;
  node_ = node;
  ibv_context* ctx = node_->get_ctx().get();
  ibv_pd* pd = node_->get_pd().get();

  // scq_ = ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0);
  scq_ = std::shared_ptr<ibv_cq>(ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0),
                                 [](ibv_cq* p){ ibv_destroy_cq(p); });
  if (!scq_) {
    rdma_log(
        RDMA_ERROR,
        "RDMAConnEvent::RDMAConnEvent, failed to create CQ for a connection");
    exit(-1);
  }

  if (recv_channel) {
    // rcq_ = ibv_create_cq(ctx, DEFAULT_CQE, NULL, recv_channel, 0);
    rcq_ = std::shared_ptr<ibv_cq>(ibv_create_cq(ctx, DEFAULT_CQE, NULL, recv_channel, 0),
                                   [](ibv_cq* p){ ibv_destroy_cq(p); });
  } else {
    recv_channel_ = ibv_create_comp_channel(ctx);
    int recv_flags = fcntl(recv_channel_->fd, F_GETFL);
    if (fcntl(recv_channel_->fd, F_SETFL, recv_flags | O_NONBLOCK) < 0) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::RDMAConnEvent, failed to change channel fd to non-blocking");
      exit(-1);
    }
    // rcq_ = ibv_create_cq(ctx, DEFAULT_CQE, NULL, recv_channel_, 0);
    rcq_ = std::shared_ptr<ibv_cq>(ibv_create_cq(ctx, DEFAULT_CQE, NULL, recv_channel_, 0),
                                   [](ibv_cq* p){ ibv_destroy_cq(p); });
  }
  if (!rcq_) {
    rdma_log(
        RDMA_ERROR,
        "RDMAConnEvent::RDMAConnEvent, failed to create CQ for a connection");
    exit(-1);
  }
  ibv_req_notify_cq(rcq_.get(), 0);

  memset(&qp_attr_, 0, sizeof(qp_attr_));
  qp_attr_.recv_cq = rcq_.get();
  qp_attr_.send_cq = scq_.get();
  qp_attr_.qp_type = IBV_QPT_RC;
  qp_attr_.sq_sig_all = 0;
  qp_attr_.cap.max_send_wr = DEFAULT_MAX_SEND_WR;
  qp_attr_.cap.max_recv_wr = DEFAULT_MAX_RECV_WR;
  qp_attr_.cap.max_send_sge = DEFAULT_MAX_SEND_SGE;
  qp_attr_.cap.max_recv_sge = DEFAULT_MAX_RECV_SGE;
  qp_ = std::shared_ptr<ibv_qp>(ibv_create_qp(pd, &qp_attr_),
                                [](ibv_qp* p) { ibv_destroy_qp(p); });
  if (!qp_) {
    rdma_log(
        RDMA_ERROR,
        "RDMAConnEvent::RDMAConnEvent, failed to create QP for a connection");
    exit(-1);
  }

  sync();
}

RDMAConnEvent::~RDMAConnEvent() {
  if (recv_channel_) {
    if (unacked_recv_events_num_) {
      ibv_ack_cq_events(rcq_.get(), unacked_recv_events_num_);
    }
    ibv_destroy_comp_channel(recv_channel_);
  }
  if (send_channel_) {
    if (unacked_send_events_num_) {
      ibv_ack_cq_events(rcq_.get(), unacked_send_events_num_);
    }
    ibv_destroy_comp_channel(send_channel_);
  }
}

size_t RDMAConnEvent::get_send_events_locked() {
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  if (ibv_get_cq_event(send_channel_, &cq, &ev_ctx) == -1) return 0;
  if (cq != scq_.get()) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::get_send_events_locked, unknown CQ got event");
    exit(-1);
  }
  if (ibv_req_notify_cq(cq, 0)) {
    rdma_log(
        RDMA_ERROR,
        "RDMAConnEvent::get_event_locked, require notifcation on rcq failed");
    exit(-1);
  }
  unacked_send_events_num_++;
  if (unacked_send_events_num_ >= DEFAULT_EVENT_ACK_LIMIT) {
    ibv_ack_cq_events(scq_.get(), unacked_send_events_num_);
    unacked_send_events_num_ = 0;
  }
  return poll_send_completions();
}

size_t RDMAConnEvent::get_recv_events_locked(uint8_t* addr, size_t length, uint32_t lkey) {
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  if (ibv_get_cq_event(recv_channel_, &cq, &ev_ctx) == -1) return 0;
  if (cq != rcq_.get()) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::get_recv_events_locked, unknown CQ got event");
    exit(-1);
  }
  if (ibv_req_notify_cq(cq, 0)) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::get_recv_events_locked, require notifcation on rcq failed");
    exit(-1);
  }
  unacked_recv_events_num_++;
  if (unacked_recv_events_num_ >= DEFAULT_EVENT_ACK_LIMIT) {
    ibv_ack_cq_events(rcq_.get(), unacked_recv_events_num_);
    unacked_recv_events_num_ = 0;
  }
  return poll_recv_completions_and_post_recvs(addr, length, lkey);
}

size_t RDMAConnEvent::poll_recv_completions_and_post_recvs(uint8_t* addr,
                                                           size_t length,
                                                           uint32_t lkey) {
  int recv_bytes = 0;
  int completed = ibv_poll_cq(rcq_.get(), DEFAULT_MAX_POST_RECV, recv_wcs_);

  // printf("poll_recv_completions_and_post_recvs, completed = %lld\n", completed);

  if (completed < 0) {
    rdma_log(RDMA_ERROR,
             "RDMAConnEvent::poll_recv_completion, ibv_poll_cq return %d",
             completed);
    exit(-1);
  }
  if (completed == 0) {
    rdma_log(RDMA_WARNING,
             "RDMAConnEvent::poll_recv_completion, ibv_poll_cq return 0");
    return 0;
  }
  for (int i = 0; i < completed; i++) {
    if (recv_wcs_[i].status != IBV_WC_SUCCESS) {
      rdma_log(RDMA_ERROR,
               "RDMAConnEvent::poll_recv_completion, wc[%d] status = %d", i,
               recv_wcs_[i].status);
    }
    recv_bytes += recv_wcs_[i].byte_len;
  }

  rr_garbage_ += completed;

  rdma_log(RDMA_DEBUG,
           "RDMAConnEvent::poll_recv_completions_and_post_recvs, poll out %d "
           "completion (%d bytes) from rcq",
           completed, recv_bytes);

  if (completed == DEFAULT_MAX_POST_RECV) {
    return recv_bytes +
           poll_recv_completions_and_post_recvs(addr, length, lkey);
  }

  return recv_bytes;
}

void RDMAConnEvent::post_recvs(uint8_t* addr, size_t length, uint32_t lkey,
                               size_t n = 1) {
  if (n == 0) return;
  // struct ibv_recv_wr rr;
  // struct ibv_sge sge;
  struct ibv_recv_wr* bad_wr = NULL;

  // INIT_SGE(&sge, addr, length, lkey);
  // INIT_RR(&rr, &sge);

  // posted_rr_num_ = (posted_rr_num_ + n) % DEFAULT_MAX_POST_RECV;

  while (n--) {
    INIT_SGE(&recv_sges_[rr_tail_], addr, length, lkey);
    INIT_RR(&recv_wrs_[rr_tail_], &recv_sges_[rr_tail_]);
    int ret = ibv_post_recv(qp_.get(), &recv_wrs_[rr_tail_], &bad_wr);
    if (ret) {
      rdma_log(RDMA_ERROR, "Failed to post RR, errno = %d, wr_id = %d", ret,
               bad_wr->wr_id);
      exit(-1);
    }
    rr_tail_ = (rr_tail_ + 1) % DEFAULT_MAX_POST_RECV;
  }
}

int RDMAConnEvent::post_send(MemRegion& remote_mr, size_t remote_tail, 
                              MemRegion& local_mr, size_t local_offset,
                              size_t sz, ibv_wr_opcode opcode) {
  if (remote_mr.is_local() || local_mr.is_remote()) {
    rdma_log(RDMA_ERROR, "RDMAConn::post_send_and_poll_completion, MemRegion incorrect");
    exit(-1);
  }

  size_t remote_cap = remote_mr.length();
  size_t r_len = MIN(remote_cap - remote_tail, sz);
  size_t avail_sr_num = (sr_tail_ - sr_head_ + DEFAULT_MAX_POST_SEND) % DEFAULT_MAX_POST_SEND;
  struct ibv_send_wr* bad_wr = nullptr;
  if (r_len == sz) { // need one sr
    if (avail_sr_num < 1) return 0;
    INIT_SGE(&send_sges_[sr_tail_], (uint8_t*)local_mr.addr() + local_offset, sz, local_mr.lkey());
    INIT_SR(&send_wrs_[sr_tail_], &send_sges_[sr_tail_], opcode, (uint8_t*)remote_mr.addr() + remote_tail, remote_mr.rkey(), 1, sz);
    if (ibv_post_send(qp_.get(), &send_wrs_[sr_tail_], &bad_wr) != 0) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::post_send, failed to post send");
      exit(-1);
    }
    sr_tail_ = (sr_tail_ + 1) % DEFAULT_MAX_POST_SEND;
    return 1;
  } 

  // need two srs
  if (avail_sr_num < 2) return 0;
  INIT_SGE(&send_sges_[sr_tail_], (uint8_t*)local_mr.addr() + local_offset, r_len, local_mr.lkey());
  INIT_SR(&send_wrs_[sr_tail_], &send_sges_[sr_tail_], opcode, (uint8_t*)remote_mr.addr() + remote_tail, remote_mr.rkey(), 1, r_len);
  size_t second_sr_id = (sr_tail_ + 1) % DEFAULT_MAX_POST_SEND;
  INIT_SGE(&send_sges_[second_sr_id], (uint8_t*)local_mr.addr() + local_offset + r_len, sz - r_len, local_mr.lkey());
  INIT_SR(&send_wrs_[second_sr_id], &send_sges_[second_sr_id], opcode, remote_mr.addr(), remote_mr.rkey(), 1, sz - r_len);
  send_wrs_[sr_tail_].next = &send_wrs_[second_sr_id];
  if (ibv_post_send(qp_.get(), &send_wrs_[sr_tail_], &bad_wr) != 0) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::post_send, failed to post send");
    exit(-1);
  }
  sr_tail_ = (sr_tail_ + 2) % DEFAULT_MAX_POST_SEND;

  return 2;
}

size_t RDMAConnEvent::poll_send_completions() {
  int completed = ibv_poll_cq(scq_.get(), DEFAULT_MAX_POST_SEND, send_wcs_);

  if (completed < 0) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::poll_send_completions, ibv_poll_cq return %d", completed);
    exit(-1);
  }
  if (completed == 0) {
    rdma_log(RDMA_WARNING, "RDMAConnEvent::poll_send_completions, ibv_poll_cq return 0");
    return 0;
  }
  size_t send_bytes = 0;
  for (int i = 0; i < completed; i++) {
    if (send_wcs_[i].status != IBV_WC_SUCCESS) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::poll_send_completion, wc[%d] status = %d",
               i, send_wcs_[i].status);
    }
    send_bytes += send_wcs_[i].imm_data;
  }
  sr_head_ = (sr_head_ + completed) % DEFAULT_MAX_POST_SEND;
  return send_bytes;
}
