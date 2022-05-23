#include "RDMAConn.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <condition_variable>
#include "grpc/impl/codegen/log.h"
#include "src/core/lib/debug/trace.h"
grpc_core::TraceFlag grpc_rdma_conn_trace(false, "rdma_conn");
extern bool is_server;
RDMAConn::RDMAConn(RDMANode* node, bool event_mode) : node_(node) {
  ibv_context* ctx = node_->get_ctx().get();
  ibv_pd* pd = node_->get_pd().get();

  scq_ = std::shared_ptr<ibv_cq>(ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0),
                                 [](ibv_cq* p) { ibv_destroy_cq(p); });
  if (!scq_) {
    gpr_log(GPR_ERROR, "Failed to create CQ for a connection");
    exit(-1);
  }
  if (event_mode) {
    recv_channel_ = std::shared_ptr<ibv_comp_channel>(
        ibv_create_comp_channel(ctx), [](ibv_comp_channel* recv_channel) {
          ibv_destroy_comp_channel(recv_channel);
        });
    int recv_flags = fcntl(recv_channel_->fd, F_GETFL);
    if (fcntl(recv_channel_->fd, F_SETFL, recv_flags | O_NONBLOCK) < 0) {
      gpr_log(GPR_ERROR,
              "RDMAConnEvent::RDMAConnEvent, failed to change channel fd to "
              "non-blocking");
      exit(-1);
    }
  }

  rcq_ = std::shared_ptr<ibv_cq>(
      ibv_create_cq(ctx, DEFAULT_CQE, nullptr, recv_channel_.get(), 0),
      [](ibv_cq* p) { ibv_destroy_cq(p); });
//  if (event_mode && is_server) {
//    ibv_modify_cq_attr attr;
//    memset(&attr, 0, sizeof(attr));
//    attr.attr_mask = IBV_CQ_ATTR_MODERATE;
//    attr.moderate.cq_count = 32767;
//    attr.moderate.cq_period = 40;
//    int err = ibv_modify_cq(rcq_.get(), &attr);
//    if (err != 0) {
//      gpr_log(GPR_ERROR, "Err modify cq: %d", err);
//    }
//  }

  if (!rcq_) {
    gpr_log(GPR_ERROR, "Failed to create CQ for a connection");
    exit(-1);
  }

  if (event_mode) {
    ibv_req_notify_cq(rcq_.get(), 0);
  }

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
  if (qp_ == nullptr) {
    gpr_log(GPR_ERROR, "Failed to create QP for a connection");
    exit(-1);
  }
}

RDMAConn::~RDMAConn() {
  if (unacked_recv_events_num_) {
    ibv_ack_cq_events(rcq_.get(), unacked_recv_events_num_);
  }
}

int RDMAConn::SyncMR(int fd, MemRegion& local, MemRegion& remote) {
  struct {
    void* addr;
    uint32_t rkey;
    size_t length;
  } lo = {local.addr(), local.rkey(), local.length()}, rt;

  if (sync_data(fd, (char*)&lo, (char*)&rt, sizeof(lo))) {
    gpr_log(GPR_ERROR, "RDMAConn::SyncMR, failed to exchange MR information");
    exit(1);
  }

  return remote.remote_reg(rt.addr, rt.rkey, rt.length);
}

int RDMAConn::SyncQP(int fd) {
  ibv_port_attr port_attr = node_->get_port_attr();
  union ibv_gid gid = node_->get_gid();

  struct {
    uint32_t qp_num;
    uint16_t lid;
    uint8_t gid[16];
  } local = {qp_->qp_num, port_attr.lid}, remote;
  memcpy(&local.gid, &gid, sizeof(gid));

  // exchange data for nodes
  if (sync_data(fd, (char*)&local, (char*)&remote, sizeof(local))) {
    gpr_log(GPR_ERROR,
            "RDMAConn::sync, failed to exchange QP data and the initial MR");
    exit(-1);
  }

  qp_num_rt_ = remote.qp_num;
  lid_rt_ = remote.lid;
  memcpy(&gid_rt_, remote.gid, sizeof(gid_rt_));

  if (modify_state(INIT)) {
    gpr_log(GPR_ERROR, "RDMAConn::sync, failed to change to INIT state");
    exit(-1);
  }

  if (modify_state(RTR)) {
    gpr_log(GPR_ERROR, "DMAConn::sync, failed to change to RTR state");
    exit(-1);
  }

  if (modify_state(RTS)) {
    gpr_log(GPR_ERROR, "DMAConn::sync, failed to change to RTS state");
    exit(-1);
  }

  barrier(fd);

  return 0;
}

int RDMAConn::post_send(MemRegion& remote_mr, size_t remote_tail,
                        MemRegion& local_mr, size_t local_offset, size_t sz,
                        ibv_wr_opcode opcode) {
  GPR_DEBUG_ASSERT(!remote_mr.is_local() && !local_mr.is_remote());

  size_t remote_cap = remote_mr.length();
  size_t r_len = MIN(remote_cap - remote_tail, sz);
  struct ibv_send_wr* bad_wr = nullptr;

  if (r_len == sz) {  // need one sr
    ibv_sge sge;
    ibv_send_wr sr;
    init_sge(&sge, static_cast<uint8_t*>(local_mr.addr()) + local_offset, sz,
             local_mr.lkey());
    init_sr(&sr, &sge, opcode,
            static_cast<uint8_t*>(remote_mr.addr()) + remote_tail,
            remote_mr.rkey(), 1, sz, 0, nullptr);
    //    GPR_ASSERT(sz > 0);
    if (ibv_post_send(qp_.get(), &sr, &bad_wr) != 0) {
      gpr_log(GPR_ERROR, "Failed to post send");
      exit(-1);
    }
    return 1;
  }

  // need two srs
  ibv_sge sge1, sge2;
  ibv_send_wr sr1, sr2;

  init_sge(&sge1, static_cast<uint8_t*>(local_mr.addr()) + local_offset, r_len,
           local_mr.lkey());
  init_sr(&sr1, &sge1, opcode,
          static_cast<uint8_t*>(remote_mr.addr()) + remote_tail,
          remote_mr.rkey(), 1, r_len, 0, &sr2);
  init_sge(&sge2, static_cast<uint8_t*>(local_mr.addr()) + local_offset + r_len,
           sz - r_len, local_mr.lkey());
  init_sr(&sr2, &sge2, opcode, remote_mr.addr(), remote_mr.rkey(), 1,
          sz - r_len, 0, nullptr);
  if (ibv_post_send(qp_.get(), &sr1, &bad_wr) != 0) {
    gpr_log(GPR_ERROR, "Failed to post send");
    exit(-1);
  }

  return 2;
}

int RDMAConn::post_sends(MemRegion& remote_mr, size_t remote_tail,
                         struct ibv_sge* sg_list, size_t num_sge, size_t sz,
                         ibv_wr_opcode opcode) {
  size_t remote_cap = remote_mr.length();
  size_t r_len = MIN(remote_cap - remote_tail, sz);

  int _num_sge_ = 0;
  size_t nwritten = 0;
  while (_num_sge_ < DEFAULT_MAX_SEND_SGE && nwritten < r_len) {
    nwritten += sg_list[_num_sge_].length;
    _num_sge_++;
  }

  // 0 < _num_sge_: at least enter while loop once
  // _num_sge_ <= DEFAULT_MAX_SEND_SGE: max is DEFAULT_MAX_SEND_SGE
  // _num_sge_ <= num_sge: nwritten <= sz. when nwritten == sz, _num_sge_ ==
  // num_sge

  struct ibv_send_wr sr;
  struct ibv_sge* next_sg_list = nullptr;
  struct ibv_send_wr* bad_wr = nullptr;
  size_t next_num_sge = 0;
  if (nwritten <= r_len) {
    init_sr(&sr, sg_list, opcode, (uint8_t*)remote_mr.addr() + remote_tail,
            remote_mr.rkey(), _num_sge_, nwritten, 0, nullptr);
    if (ibv_post_send(qp_.get(), &sr, &bad_wr) != 0) {
      exit(-1);
    }
    if (_num_sge_ <
        num_sge) {  // next send: sg_list[_num_sge_], ..., sg_list[num_sge - 1]
      next_sg_list = &(sg_list[_num_sge_]);
      next_num_sge = num_sge - _num_sge_;
    }
  } else {  // nwritten > r_len
    size_t extra = nwritten - r_len;
    nwritten = r_len;
    sg_list[_num_sge_ - 1].length -=
        extra;  // cut off extra size, send first half
    init_sr(&sr, sg_list, opcode, (uint8_t*)remote_mr.addr() + remote_tail,
            remote_mr.rkey(), _num_sge_, nwritten, 0, nullptr);
    if (ibv_post_send(qp_.get(), &sr, &bad_wr) != 0) {
      exit(-1);
    }
    // reuse sg_list[_num_sge_ - 1] in next send
    sg_list[_num_sge_ - 1].addr +=
        sg_list[_num_sge_ - 1].length;      // addr move forward first half size
    sg_list[_num_sge_ - 1].length = extra;  // set length as extra size.
    next_sg_list = &(sg_list[_num_sge_ - 1]);  // next send: sg_list[_num_sge_ -
                                               // 1], ..., sg_list[num_sge - 1]
    next_num_sge = num_sge - _num_sge_ + 1;
  }

  remote_tail = (remote_tail + nwritten) % remote_cap;
  sz -= nwritten;

  if (sz == 0) return 1;
  return 1 + post_sends(remote_mr, remote_tail, next_sg_list, next_num_sge, sz,
                        opcode);
}

void RDMAConn::poll_send_completion(int expected_num_entries) {
  if (expected_num_entries == 0) {
    return;
  } else if (expected_num_entries > DEFAULT_MAX_POST_SEND) {
    gpr_log(
        GPR_ERROR,
        "RDMAConn::poll_send_completion, expected_num_entries is too large");
    exit(-1);
  }

  int rest_num_entries = expected_num_entries;
  ibv_wc wc[DEFAULT_MAX_POST_SEND];

  do {
    int n = ibv_poll_cq(scq_.get(), rest_num_entries,
                        wc + expected_num_entries - rest_num_entries);
    if (n < 0) {
      gpr_log(GPR_ERROR, "RDMAConn::poll_send_completion, failed to poll scq");
      exit(-1);
    }
    rest_num_entries -= n;
  } while (rest_num_entries > 0);

  for (int i = 0; i < expected_num_entries; i++) {
    if (wc[i].status != IBV_WC_SUCCESS) {
      gpr_log(GPR_ERROR,
              "RDMAConn::poll_send_completion, failed to poll scq, status %d",
              wc[i].status);
      exit(-1);
    }
  }
}

void RDMAConn::post_recvs(size_t n) {
  if (n == 0) return;
  struct ibv_recv_wr* bad_wr = nullptr;

  while (n--) {
    ibv_sge sge;
    ibv_recv_wr wr;
    init_sge(&sge, nullptr, 0, 0);
    init_rr(&wr, &sge, 1);
    int ret = ibv_post_recv(qp_.get(), &wr, &bad_wr);
    if (ret) {
      gpr_log(GPR_ERROR, "Failed to post RR, errno = %d, wr_id = %lu", ret,
              bad_wr->wr_id);
      exit(-1);
    }
    rr_tail_ = (rr_tail_ + 1) % DEFAULT_MAX_POST_RECV;
  }
}

size_t RDMAConn::poll_recv_completion() {
  int recv_bytes = 0;
  ibv_wc wc[DEFAULT_MAX_POST_RECV];
  int completed = ibv_poll_cq(rcq_.get(), DEFAULT_MAX_POST_RECV, wc);

  if (completed < 0) {
    gpr_log(GPR_ERROR,
            "RDMAConnEvent::poll_recv_completion, ibv_poll_cq return %d",
            completed);
    exit(-1);
  }
  if (completed == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_conn_trace)) {
      gpr_log(GPR_INFO,
              "RDMAConnEvent::poll_recv_completion, ibv_poll_cq return 0");
    }
    return 0;
  }
  for (int i = 0; i < completed; i++) {
    if (wc[i].status != IBV_WC_SUCCESS) {
      gpr_log(GPR_ERROR,
              "RDMAConnEvent::poll_recv_completion, wc[%d] status = %d", i,
              wc[i].status);
    }
    recv_bytes += wc[i].byte_len;
  }

  rr_garbage_ += completed;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_conn_trace)) {
    gpr_log(GPR_INFO,
            "RDMAConnEvent::poll_recv_completions_and_post_recvs, poll out %d "
            "completion (%d bytes) from rcq",
            completed, recv_bytes);
  }

  if (completed == DEFAULT_MAX_POST_RECV) {
    return recv_bytes + poll_recv_completion();
  }
  return recv_bytes;
}

size_t RDMAConn::get_recv_events_locked() {
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  if (ibv_get_cq_event(recv_channel_.get(), &cq, &ev_ctx) == -1) return 0;
  if (cq != rcq_.get()) {
    gpr_log(GPR_ERROR,
            "RDMAConnEvent::get_recv_events_locked, unknown CQ got event");
    exit(-1);
  }
  if (ibv_req_notify_cq(cq, 0)) {
    gpr_log(GPR_ERROR,
            "RDMAConnEvent::get_recv_events_locked, require notifcation on "
            "rcq failed");
    exit(-1);
  }
  if (unacked_recv_events_num_++ >= DEFAULT_EVENT_ACK_LIMIT) {
    ibv_ack_cq_events(rcq_.get(), unacked_recv_events_num_);
    unacked_recv_events_num_ = 0;
  }
  return poll_recv_completion();
}
