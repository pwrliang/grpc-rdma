#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "grpc/impl/codegen/log.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/rdma/rdma_conn.h"
grpc_core::TraceFlag grpc_rdma_conn_trace(false, "rdma_conn");

RDMAConn::RDMAConn(int fd, RDMANode* node, const char* name, bool event_mode)
    : fd_(fd), node_(node), name_(name) {
  ibv_context* ctx = node_->get_ctx().get();
  ibv_pd* pd = node_->get_pd().get();

  scq_ = std::shared_ptr<ibv_cq>(ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0),
                                 [](ibv_cq* p) { ibv_destroy_cq(p); });
  if (!scq_) {
    gpr_log(GPR_ERROR, "Channel: %s Failed to create CQ for a connection",
            name_);
    abort();
  }

  if (event_mode) {
    recv_channel_ = std::shared_ptr<ibv_comp_channel>(
        ibv_create_comp_channel(ctx), [](ibv_comp_channel* recv_channel) {
          ibv_destroy_comp_channel(recv_channel);
        });
    int recv_flags = fcntl(recv_channel_->fd, F_GETFL);
    if (fcntl(recv_channel_->fd, F_SETFL, recv_flags | O_NONBLOCK) < 0) {
      gpr_log(GPR_ERROR,
              "Channel: %s Failed to change channel fd to non-blocking", name_);
      abort();
    }
  }

  rcq_ = std::shared_ptr<ibv_cq>(
      ibv_create_cq(ctx, DEFAULT_CQE, nullptr, recv_channel_.get(), 0),
      [](ibv_cq* p) { ibv_destroy_cq(p); });
  //  if (event_mode && is_server) {
  //    ibv_modify_cq_attr attr;
  //    memset(&attr, 0, sizeof(attr));
  //    attr.attr_mask = IBV_CQ_ATTR_MODERATE;
  //    attr.moderate.cq_count = 5;
  //    attr.moderate.cq_period = 40;
  //    int err = ibv_modify_cq(rcq_.get(), &attr);
  //    if (err != 0) {
  //      gpr_log(GPR_ERROR, "Err modify cq: %d", err);
  //    }
  //  }

  if (!rcq_) {
    gpr_log(GPR_ERROR, "Channel: %s, Failed to create CQ for a connection",
            name_);
    abort();
  }

  if (event_mode) {
    ibv_req_notify_cq(rcq_.get(), 0);
  }

  memset(&qp_attr_, 0, sizeof(qp_attr_));
  qp_attr_.recv_cq = rcq_.get();
  qp_attr_.send_cq = scq_.get();
  qp_attr_.qp_type = IBV_QPT_RC;
  qp_attr_.sq_sig_all =
      1;  // send_flags should be IBV_SEND_SIGNALED if sq_sig_all=0;
  qp_attr_.cap.max_send_wr = DEFAULT_MAX_SEND_WR;
  qp_attr_.cap.max_recv_wr = DEFAULT_MAX_RECV_WR;
  qp_attr_.cap.max_send_sge = DEFAULT_MAX_SEND_SGE;
  qp_attr_.cap.max_recv_sge = DEFAULT_MAX_RECV_SGE;
  qp_ = std::shared_ptr<ibv_qp>(ibv_create_qp(pd, &qp_attr_),
                                [](ibv_qp* p) { ibv_destroy_qp(p); });
  if (qp_ == nullptr) {
    gpr_log(GPR_ERROR, "Channel: %s, Failed to create QP for a connection",
            name_);
    abort();
  }
}

RDMAConn::~RDMAConn() {
  if (recv_channel_ != nullptr) {
    if (unack_cqe_) {
      ibv_ack_cq_events(rcq_.get(), unack_cqe_);
    }
    close(recv_channel_->fd);
  }
}

int RDMAConn::SyncMR(const MemRegion& local, MemRegion& remote) {
  struct {
    void* addr;
    uint32_t rkey;
    size_t length;
  } lo = {local.addr(), local.rkey(), local.length()}, rt;

  if (sync_data(fd_, (char*)&lo, (char*)&rt, sizeof(lo))) {
    gpr_log(GPR_ERROR,
            "Channel: %s, RDMAConn::SyncMR, failed to exchange MR information",
            name_);
    abort();
  }

  return remote.RegisterRemote(rt.addr, rt.rkey, rt.length);
}

int RDMAConn::SyncQP() {
  ibv_port_attr port_attr = node_->get_port_attr();
  union ibv_gid gid = node_->get_gid();
  srand48(time(NULL));
  uint32_t psn = static_cast<uint32_t>(lrand48() & 0xffffff);

  struct {
    uint32_t qp_num;
    uint16_t lid;
    uint32_t psn;
    uint8_t gid[16];
  } local = {qp_->qp_num, port_attr.lid, psn}, remote;
  memcpy(&local.gid, &gid, sizeof(gid));

  // exchange data for nodes
  if (sync_data(fd_, (char*)&local, (char*)&remote, sizeof(local))) {
    gpr_log(GPR_ERROR,
            "Channel: %s, RDMAConn::sync, failed to exchange QP data and the "
            "initial MR",
            name_);
    abort();
  }

  qp_num_rt_ = remote.qp_num;
  lid_rt_ = remote.lid;
  psn_rt_ = remote.psn;
  memcpy(&gid_rt_, remote.gid, sizeof(gid_rt_));

  if (modify_qp_to_init(qp_.get())) {
    gpr_log(GPR_ERROR,
            "Channel: %s, RDMAConn::sync, failed to change to INIT state",
            name_);
    abort();
  }

  if (modify_qp_to_rtr(qp_.get(), qp_num_rt_, psn_rt_, lid_rt_, gid_rt_,
                       node_->get_port_attr().link_layer)) {
    gpr_log(GPR_ERROR,
            "Channel: %s, RDMAConn::sync, failed to change to RTR state",
            name_);
    abort();
  }

  if (modify_qp_to_rts(qp_.get(), psn)) {
    gpr_log(GPR_ERROR,
            "Channel: %s, RDMAConn::sync, failed to change to RTS state",
            name_);
    abort();
  }

  // Add a barrier to make sure that the peer has been modified to RTR and RTS
  barrier(fd_);

  return 0;
}

int RDMAConn::PostSendRequest(MemRegion& remote_mr, MemRegion& local_mr,
                              size_t sz, ibv_wr_opcode opcode) {
  GPR_DEBUG_ASSERT(!remote_mr.is_local() && !local_mr.is_remote());

  struct ibv_send_wr* bad_wr = nullptr;

  ibv_sge sge;
  ibv_send_wr sr;
  init_sge(&sge, static_cast<uint8_t*>(local_mr.addr()), sz, local_mr.lkey());
  init_sr(&sr, &sge, opcode, static_cast<uint8_t*>(remote_mr.addr()),
          remote_mr.rkey(), 1, 0, 0, nullptr);

  if (ibv_post_send(qp_.get(), &sr, &bad_wr) != 0) {
    gpr_log(GPR_ERROR, "Channel: %s, Failed to post send", name_);
    abort();
  }
  return 1;
}

int RDMAConn::PostSendRequest(MemRegion& remote_mr, size_t remote_tail,
                              MemRegion& local_mr, size_t local_offset,
                              size_t sz, ibv_wr_opcode opcode) {
  GPR_DEBUG_ASSERT(!remote_mr.is_local() && !local_mr.is_remote());
  GPR_ASSERT(sz > 0);
  size_t remote_cap = remote_mr.length();
  GPR_ASSERT(remote_tail <= remote_cap);
  size_t r_len = MIN(remote_cap - remote_tail, sz);
  struct ibv_send_wr* bad_wr = nullptr;

  if (r_len == sz) {  // need one sr
    ibv_sge sge;
    ibv_send_wr sr;
    init_sge(&sge, static_cast<uint8_t*>(local_mr.addr()) + local_offset, sz,
             local_mr.lkey());
    init_sr(&sr, &sge, opcode,
            static_cast<uint8_t*>(remote_mr.addr()) + remote_tail,
            remote_mr.rkey(), 1, sz, sz, nullptr);
    int err = ibv_post_send(qp_.get(), &sr, &bad_wr);
    if (err != 0) {
      gpr_log(GPR_ERROR, "Channel: %s, Failed to post send, err: %d", name_,
              err);
      abort();
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
          remote_mr.rkey(), 1, r_len, r_len, &sr2);
  init_sge(&sge2, static_cast<uint8_t*>(local_mr.addr()) + local_offset + r_len,
           sz - r_len, local_mr.lkey());
  init_sr(&sr2, &sge2, opcode, remote_mr.addr(), remote_mr.rkey(), 1,
          sz - r_len, sz - r_len, nullptr);
  int err = ibv_post_send(qp_.get(), &sr1, &bad_wr);
  if (err != 0) {
    gpr_log(GPR_ERROR, "Channel: %s, Failed to post send, err: %d", name_, err);
    abort();
  }

  return 2;
}

int RDMAConn::PostSendRequests(MemRegion& remote_mr, size_t remote_tail,
                               struct ibv_sge* sg_list, size_t num_sge,
                               size_t sz, ibv_wr_opcode opcode) {
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
      abort();
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
      abort();
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
  return 1 + PostSendRequests(remote_mr, remote_tail, next_sg_list,
                              next_num_sge, sz, opcode);
}

int RDMAConn::PollSendCompletion(int expected_num_entries,
                                 size_t* sent_size_bytes) {
  int initial_expected_num_entries = expected_num_entries;
  if (sent_size_bytes != nullptr) {
    *sent_size_bytes = 0;
  }
  while (expected_num_entries > 0) {
    ibv_wc wc[DEFAULT_MAX_POST_SEND];
    int r;

    while ((r = ibv_poll_cq(scq_.get(),
                            MIN(DEFAULT_MAX_POST_SEND, expected_num_entries),
                            wc)) > 0) {
      for (int i = 0; i < r; i++) {
        if (wc[i].status != IBV_WC_SUCCESS) {
          gpr_log(GPR_ERROR, "Channel: %s, PollSendCompletion, wc status = %d",
                  name_, wc[i].status);
          return wc[i].status;
        }

        if (sent_size_bytes != nullptr) {
          *sent_size_bytes += wc[i].wr_id;
        }
      }
      expected_num_entries -= r;
      if (expected_num_entries < 0) {
        gpr_log(GPR_ERROR,
                "Channel: %s, initial_expected_num_entries: %d, "
                "curr_expected_num_entries: %d",
                name_, initial_expected_num_entries, expected_num_entries);
      }
      GPR_ASSERT(expected_num_entries >= 0);
    }

    if (r < 0) {
      gpr_log(GPR_ERROR,
              "Channel: %s, PollSendCompletion, ibv_poll_cq return %d", name_,
              r);
      return r;
    }
  }
  return 0;
}

void RDMAConn::PostRecvRequests(size_t n) {
  if (n == 0) return;
  struct ibv_recv_wr* bad_wr = nullptr;

  while (n--) {
    ibv_sge sge;
    ibv_recv_wr wr;
    init_sge(&sge, nullptr, 0, 0);
    init_rr(&wr, &sge, 1);
    int ret = ibv_post_recv(qp_.get(), &wr, &bad_wr);
    if (ret) {
      gpr_log(
          GPR_ERROR,
          "Channel: %s, Failed to post RR, errno = %d, wr_id = %lu, this: %p",
          name_, ret, bad_wr->wr_id, this);
      abort();
    }
    rr_tail_ = (rr_tail_ + 1) % DEFAULT_MAX_POST_RECV;
  }
}

size_t RDMAConn::PollRecvCompletion() {
  int recv_bytes = 0;
  ibv_wc wc[DEFAULT_MAX_POST_RECV];
  int r;

  while ((r = ibv_poll_cq(rcq_.get(), DEFAULT_MAX_POST_RECV, wc)) > 0) {
    for (int i = 0; i < r; i++) {
      if (wc[i].status != IBV_WC_SUCCESS) {
        gpr_log(GPR_ERROR, "Channel: %s, PollRecvCompletion, wc status = %d",
                name_, wc[i].status);
      }
      recv_bytes += wc[i].byte_len;
      rr_garbage_++;
    }
  }

  if (r < 0) {
    gpr_log(GPR_ERROR, "Channel: %s, PollRecvCompletion, ibv_poll_cq return %d",
            name_, r);
    abort();
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_rdma_conn_trace)) {
    gpr_log(GPR_INFO, "PollRecvCompletion, imm: %d bytes", recv_bytes);
  }
  return recv_bytes;
}

size_t RDMAConn::GetRecvEvents() {
  ibv_cq* cq = nullptr;
  void* ev_ctx = nullptr;
  int r = ibv_get_cq_event(recv_channel_.get(), &cq, &ev_ctx);

  if (r == -1) {
    // todo: print errno?
    return 0;
  }
  if (cq != rcq_.get()) {
    gpr_log(GPR_ERROR,
            "Channel: %s, RDMAConnEvent::GetRecvEvents, unknown CQ got event",
            name_);
    abort();
  }
  if (ibv_req_notify_cq(cq, 0)) {
    gpr_log(GPR_ERROR,
            "Channel: %s, GetRecvEvents, require notification on failed",
            name_);
    abort();
  }
  if (++unack_cqe_ >= DEFAULT_EVENT_ACK_LIMIT) {
    ibv_ack_cq_events(rcq_.get(), unack_cqe_);
    unack_cqe_ = 0;
  }
  return PollRecvCompletion();
}
