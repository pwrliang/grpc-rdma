#include "RDMAConn.h"
#include "log.h"
#include <infiniband/verbs.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <assert.h>
#include "fcntl.h"

// -----< RDMAConnEvent >-----

RDMAConnEvent::RDMAConnEvent(int fd, RDMANode* node, RDMASenderReceiverEvent* rdmasr)
  : posted_rr_num_(0) {
  fd_ = fd;
  node_ = node;
  ibv_context* ctx = node_->get_ctx();
  ibv_pd* pd = node_->get_pd();
  ibv_port_attr port_attr = node_->get_port_attr();
  ibv_device_attr dev_attr = node_->get_device_attr();
  union ibv_gid gid = node_->get_gid();

  channel_ = ibv_create_comp_channel(ctx);
  int flags = fcntl(channel_->fd, F_GETFL);
  if (fcntl(channel_->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::RDMAConnEvent, failed to change channel fd to non-blocking");
    exit(-1);
  }

  scq_ = ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0);
  if (!scq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConnEvent::RDMAConnEvent, failed to create CQ for a connection");
    exit(-1);
  }

  rcq_ = ibv_create_cq(ctx, DEFAULT_CQE, rdmasr, channel_, 0);
  if (!rcq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConnEvent::RDMAConnEvent, failed to create CQ for a connection");
    exit(-1);
  }
  ibv_req_notify_cq(rcq_, 0);

  memset(&qp_attr_, 0, sizeof(qp_attr_));
  qp_attr_.recv_cq = rcq_;
  qp_attr_.send_cq = scq_;
  qp_attr_.qp_type = IBV_QPT_RC;
  qp_attr_.sq_sig_all = 0;
  qp_attr_.cap.max_send_wr = DEFAULT_MAX_SEND_WR;
  qp_attr_.cap.max_recv_wr = DEFAULT_MAX_RECV_WR;
  qp_attr_.cap.max_send_sge = DEFAULT_MAX_SEND_SGE;
  qp_attr_.cap.max_recv_sge = DEFAULT_MAX_RECV_SGE;
  qp_ = ibv_create_qp(pd, &qp_attr_);
  if (!qp_) {
    rdma_log(RDMA_ERROR,
             "RDMAConnEvent::RDMAConnEvent, failed to create QP for a connection");
    exit(-1);
  }

  sync();
}

RDMAConnEvent::~RDMAConnEvent() {
  if (channel_) {
    if (unacked_events_num_) {
      ibv_ack_cq_events(rcq_, unacked_events_num_);
    }
    ibv_destroy_comp_channel(channel_);
  }
}

bool RDMAConnEvent::get_event_locked() {
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  if (ibv_get_cq_event(channel_, &cq, &ev_ctx) == -1) return false;
  if (cq != rcq_) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::get_event_locked, unknown CQ got event");
    exit(-1);
  }
  unacked_events_num_++;
  if (ibv_req_notify_cq(cq, 0)) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::get_event_locked, require notifcation on rcq failed");
    exit(-1);
  }
  return true;
}

size_t RDMAConnEvent::get_events_locked(uint8_t* addr, size_t length, uint32_t lkey) {
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  while (ibv_get_cq_event(channel_, &cq, &ev_ctx) == 0) {
    if (cq != rcq_) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::get_event_locked, unknown CQ got event");
      exit(-1);
    }
    unacked_events_num_++;
    if (ibv_req_notify_cq(cq, 0)) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::get_event_locked, require notifcation on rcq failed");
      exit(-1);
    }
    cq = nullptr;
    ev_ctx = nullptr;
  }
  if (unacked_events_num_ >= DEFAULT_EVENT_ACK_LIMIT) {
    ibv_ack_cq_events(rcq_, unacked_events_num_);
    unacked_events_num_ = 0;
  }
  // ibv_cq* cq = nullptr;
  // void* ev_ctx = nullptr;
  // if (ibv_get_cq_event(channel_, &cq, &ev_ctx) == -1) return 0;
  // if (cq != rcq_) {
  //   rdma_log(RDMA_ERROR, "RDMAConnEvent::get_event_locked, unknown CQ got event");
  //   exit(-1);
  // }
  // unacked_events_num_++;
  // if (ibv_req_notify_cq(cq, 0)) {
  //   rdma_log(RDMA_ERROR, "RDMAConnEvent::get_event_locked, require notifcation on rcq failed");
  //   exit(-1);
  // }
  return poll_recv_completions_and_post_recvs(addr, length, lkey);
}

size_t RDMAConnEvent::poll_recv_completions_and_post_recvs(uint8_t* addr, size_t length, uint32_t lkey) {
  int recv_bytes = 0;
  int completed = ibv_poll_cq(rcq_, DEFAULT_MAX_POST_RECV, recv_wcs_);

  if (completed < 0) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::poll_recv_completion, ibv_poll_cq return %d", completed);
    exit(-1);
  }
  if (completed == 0) {
    rdma_log(RDMA_WARNING, "RDMAConnEvent::poll_recv_completion, ibv_poll_cq return 0");
    return 0;
  }
  for (int i = 0; i < completed; i++) {
    if (recv_wcs_[i].status != IBV_WC_SUCCESS) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::poll_recv_completion, wc[%d] status = %d",
               i, recv_wcs_[i].status);
    }
    recv_bytes += recv_wcs_[i].byte_len;
  }
  
  post_recvs(addr, length, lkey, completed);
  garbage_ += completed;


  rdma_log(RDMA_DEBUG, "RDMAConnEvent::poll_recv_completions_and_post_recvs, poll out %d completion (%d bytes) from rcq",
           completed, recv_bytes);
  
  if (completed == DEFAULT_MAX_POST_RECV) return recv_bytes + poll_recv_completions_and_post_recvs(addr, length, lkey);

  return recv_bytes;
}

void RDMAConnEvent::post_recvs(uint8_t* addr, size_t length, uint32_t lkey, size_t n = 1) {
  if (n == 0) return;
  struct ibv_recv_wr rr;
  struct ibv_sge sge;
  struct ibv_recv_wr* bad_wr = NULL;

  // INIT_SGE(&sge, addr, length, lkey);
  // INIT_RR(&rr, &sge);

  posted_rr_num_ = (posted_rr_num_ + n) % DEFAULT_MAX_POST_RECV;
  int ret;
  while (n--) {
    INIT_SGE(&recv_sges_[post_rr_tail_], addr, length, lkey);
    INIT_RR(&recv_wrs_[post_rr_tail_], &recv_sges_[post_rr_tail_]);
    ret = ibv_post_recv(qp_, &recv_wrs_[post_rr_tail_], &bad_wr);
    if (ret) {
      rdma_log(RDMA_ERROR, "Failed to post RR, errno = %d, wr_id = %d", ret,
               bad_wr->wr_id);
      exit(-1);
    }
    post_rr_tail_ = (post_rr_tail_ + 1) % DEFAULT_MAX_POST_RECV;
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
  size_t avail_sr_num = get_avail_sr_num();
  struct ibv_send_wr* bad_wr = nullptr;
  if (r_len == sz) { // need one sr
    if (avail_sr_num < 1) return -1;
    INIT_SGE(&send_sges_[posted_sr_tail_], (uint8_t*)local_mr.addr() + local_offset, sz, local_mr.lkey());
    INIT_SR(&send_wrs_[posted_sr_tail_], &send_sges_[posted_sr_tail_], opcode, (uint8_t*)remote_mr.addr() + remote_tail, remote_mr.rkey(), 1, sz);
    if (ibv_post_send(qp_, &send_wrs_[posted_sr_tail_], &bad_wr) != 0) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::post_send, failed to post send");
      exit(-1);
    }
    posted_sr_tail_ = (posted_sr_tail_ + 1) % DEFAULT_MAX_POST_SEND;
  } else { // need two srs
    if (avail_sr_num < 2) return -1;
    INIT_SGE(&send_sges_[posted_sr_tail_], (uint8_t*)local_mr.addr() + local_offset, r_len, local_mr.lkey());
    INIT_SR(&send_wrs_[posted_sr_tail_], &send_sges_[posted_sr_tail_], opcode, (uint8_t*)remote_mr.addr() + remote_tail, remote_mr.rkey(), 1, r_len);
    size_t second_sr_id = (posted_sr_tail_ + 1) % DEFAULT_MAX_POST_SEND;
    INIT_SGE(&send_sges_[second_sr_id], (uint8_t*)local_mr.addr() + local_offset + r_len, sz - r_len, local_mr.lkey());
    INIT_SR(&send_wrs_[second_sr_id], &send_sges_[second_sr_id], opcode, remote_mr.addr(), remote_mr.rkey(), 1, sz - r_len);
    send_wrs_[posted_sr_tail_].next = &send_wrs_[second_sr_id];
    if (ibv_post_send(qp_, &send_wrs_[posted_sr_tail_], &bad_wr) != 0) {
      rdma_log(RDMA_ERROR, "RDMAConnEvent::post_send, failed to post send");
      exit(-1);
    }
    posted_sr_tail_ = (posted_sr_tail_ + 2) % DEFAULT_MAX_POST_SEND;
  }
  return 0;
}