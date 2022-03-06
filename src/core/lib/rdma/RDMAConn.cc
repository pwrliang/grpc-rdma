#include "RDMAConn.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>
#include "fcntl.h"

void INIT_SGE(ibv_sge* sge, void* lc_addr, size_t sz, uint32_t lkey) {
  memset(sge, 0, sizeof(ibv_sge));
  sge->addr = (uint64_t)lc_addr;
  sge->length = sz;
  sge->lkey = lkey;
}

void INIT_SR(ibv_send_wr* sr, ibv_sge* sge, ibv_wr_opcode opcode, void* rt_addr,
             uint32_t rkey, int num_sge, uint32_t imm_data) {
  static int id = 0;
  memset(sr, 0, sizeof(ibv_send_wr));
  sr->next = NULL;
  sr->wr_id = 0;
  sr->sg_list = sge;
  sr->num_sge = num_sge;
  sr->opcode = opcode;
  sr->imm_data = imm_data;
  sr->send_flags = IBV_SEND_SIGNALED;
  sr->wr.rdma.remote_addr = (uint64_t)rt_addr;
  sr->wr.rdma.rkey = rkey;
}

void INIT_RR(ibv_recv_wr* rr, ibv_sge* sge, int num_sge) {
  static int id = 0;
  memset(rr, 0, sizeof(ibv_recv_wr));
  rr->next = NULL;
  rr->wr_id = id++;
  rr->sg_list = sge;
  rr->num_sge = num_sge;
}

static int modify_qp_to_init(ibv_qp* qp) {
  ibv_qp_attr attr;
  int flags;
  int rc;

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_INIT;
  attr.port_num = RDMANode::ib_port;
  attr.pkey_index = 0;
  attr.qp_access_flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
  flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
  rc = ibv_modify_qp(qp, &attr, flags);
  if (rc)
    rdma_log(RDMA_ERROR,
             "modify_qp_to_init, failed to modify QP state to INIT");
  return rc;
}

int modify_qp_to_rtr(struct ibv_qp* qp, uint32_t remote_qpn, uint16_t dlid,
                     union ibv_gid dgid, uint8_t link_layer) {
  struct ibv_qp_attr attr;
  int flags;
  int rc;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = IBV_MTU_1024;  // previous is IBV_MTU_1024
  attr.dest_qp_num = remote_qpn;
  attr.rq_psn = 0;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer = 12;

  if (link_layer == IBV_LINK_LAYER_INFINIBAND) {
    attr.ah_attr.is_global = 0;
  } else if (link_layer == IBV_LINK_LAYER_ETHERNET) {
    attr.ah_attr.is_global = 1;
    attr.ah_attr.grh.dgid = dgid;
    attr.ah_attr.grh.flow_label = 0;
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.sgid_index = 0;
    attr.ah_attr.grh.traffic_class = 0;
  } else {
    // UNSPECIFIED TYPE
    attr.ah_attr.is_global = 0;
  }

  attr.ah_attr.dlid = dlid;
  attr.ah_attr.sl = 0;
  attr.ah_attr.src_path_bits = 0;
  attr.ah_attr.port_num = RDMANode::ib_port;
  flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
          IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  rc = ibv_modify_qp(qp, &attr, flags);
  if (rc) {
    rdma_log(RDMA_ERROR,
             "modify_qp_to_rtr, failed to modify QP state to RTR (%d)", rc);
    rdma_log(RDMA_ERROR, "modify_qp_to_rtr, EINVAL: %d", EINVAL);
  }
  return rc;
}

int modify_qp_to_rts(struct ibv_qp* qp) {
  struct ibv_qp_attr attr;
  int flags;
  int rc;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTS;
  attr.timeout = 0x14;  // previous is 0x12
  attr.retry_cnt = 6;
  attr.rnr_retry = 6;  // previous is 0
  attr.sq_psn = 0;
  attr.max_rd_atomic = 1;
  flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
          IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
  rc = ibv_modify_qp(qp, &attr, flags);
  if (rc)
    rdma_log(RDMA_ERROR,
             "modify_qp_to_rts, failed to modify QP state to RTS\n");
  return rc;
}

// -----< RDMAConn >-----

RDMAConn::RDMAConn(int fd, RDMANode* node)
  : fd_(fd), node_(node) {
  ibv_context* ctx = node_->get_ctx();
  ibv_pd* pd = node_->get_pd();
  ibv_port_attr port_attr = node_->get_port_attr();
  ibv_device_attr dev_attr = node_->get_device_attr();
  union ibv_gid gid = node_->get_gid();

  scq_ = ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0);
  if (!scq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConn::RDMAConn, failed to create send CQ");
    exit(-1);
  }
  rcq_ = ibv_create_cq(ctx, DEFAULT_CQE, NULL, NULL, 0);
  if (!rcq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConn::RDMAConn, failed to create recv CQ");
    exit(-1);
  }

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

RDMAConn::~RDMAConn() {
  if (rcq_ && rcq_ == scq_) {
    ibv_destroy_cq(rcq_);
  } else {
    if (rcq_) ibv_destroy_cq(rcq_);
    if (scq_) ibv_destroy_cq(scq_);
  }

  if (qp_) {
    ibv_destroy_qp(qp_);
  }
}

int RDMAConn::modify_state(RDMAConn::state_t st) {
  int ret = 0;

  switch (st) {
    case RESET:
      break;
    case INIT:
      ret = modify_qp_to_init(qp_);
      break;
    case RTR:
      ret = modify_qp_to_rtr(qp_, qp_num_rt_, lid_rt_, gid_rt_,
                             node_->get_port_attr().link_layer);
      break;
    case RTS:
      ret = modify_qp_to_rts(qp_);
      break;
    default:
      rdma_log(RDMA_ERROR, "RDMAConn::modify_state, Unsupported state %d", st);
  }

  return ret;
}

int RDMAConn::sync_data(char* local, char* remote, const size_t sz) {
  size_t remain = sz;
  ssize_t done;
  if (fd_ < 3) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::sync_data, failed to sync data with remote, no opened "
             "socket(sd: %d)",
             fd_);
    return -1;
  }

  while (remain) {
    done = ::write(fd_, local + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
      } else {
        rdma_log(RDMA_ERROR, "RDMAConn::sync_data, write errno %d: %s", errno,
                 strerror(errno));
        return -1;
      }
    } else {
      remain -= done;
    }
  }

  remain = sz;
  while (remain) {
    done = ::read(fd_, remote + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
      } else {
        rdma_log(RDMA_ERROR, "RDMAConn::sync_data, read errno %d: %s", errno,
                 strerror(errno));
        return -1;
      }
    } else {
      remain -= done;
    }
  }

  return 0;
}

int RDMAConn::sync_mr(MemRegion& local, MemRegion& remote) {
  struct {
    void* addr;
    uint32_t rkey;
    size_t length;
  } lo = {local.addr(), local.rkey(), local.length()}, rt;

  if (sync_data((char*)&lo, (char*)&rt, sizeof(lo))) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::sync_mr, failed to exchange MR information");
    return 1;
  }

  return remote.remote_reg(rt.addr, rt.rkey, rt.length);
}

int RDMAConn::sync() {
  ibv_context* ctx = node_->get_ctx();
  ibv_pd* pd = node_->get_pd();
  ibv_port_attr port_attr = node_->get_port_attr();
  ibv_device_attr dev_attr = node_->get_device_attr();
  union ibv_gid gid = node_->get_gid();

  struct {
    uint32_t qp_num;
    uint16_t lid;
    uint8_t gid[16];
  } local = {qp_->qp_num, port_attr.lid}, remote;
  memcpy(&local.gid, &gid, sizeof(gid));
  
  // exchange data for nodes
  if (sync_data((char*)&local, (char*)&remote, sizeof(local))) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::sync, failed to exchange QP data and the initial MR");
    exit(-1);
  }

  qp_num_rt_ = remote.qp_num;
  lid_rt_ = remote.lid;
  memcpy(&gid_rt_, remote.gid, sizeof(gid_rt_));

  if (modify_state(INIT)) {
    rdma_log(RDMA_ERROR, "RDMAConn::sync, failed to change to INIT state");
    exit(-1);
  }

  if (modify_state(RTR)) {
    rdma_log(RDMA_ERROR, "DMAConn::sync, failed to change to RTR state");
    exit(-1);
  }

  if (modify_state(RTS)) {
    rdma_log(RDMA_ERROR, "DMAConn::sync, failed to change to RTS state");
    exit(-1);
  }

  char tmp;
  if (sync_data((char*)"s", &tmp, 1)) {
    rdma_log(RDMA_ERROR,
             "DMAConn::sync, failed to sync after switching QP to RTS");
    exit(-1);
  }

  return 0;
}

int RDMAConn::poll_send_completion() {
  int num_entries;
  ibv_wc wc;
  do {
    num_entries = ibv_poll_cq(scq_, 1, &wc);
  } while (num_entries == 0);

  if (num_entries < 0) {
    rdma_log(RDMA_ERROR, "RDMAConn::poll_send_completion, failed to poll scq");
    exit(-1);
  }
  if (wc.status != IBV_WC_SUCCESS) {
    rdma_log(RDMA_ERROR, 
             "RDMAConn::poll_send_completion, failed to poll scq, status %d, fd = %d", wc.status, fd_);
    exit(-1);
  }
  return wc.status;
}

int RDMAConn::post_send_and_poll_completion(ibv_send_wr* sr, bool update_remote) {
  // std::unique_lock<std::mutex> lck(mtx_);

  struct ibv_send_wr* bad_wr = nullptr;

  if (ibv_post_send(qp_, sr, &bad_wr) != 0) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::post_send_and_poll_completion, failed to post send");
    exit(-1);
  }

  return poll_send_completion();
}

int RDMAConn::post_send_and_poll_completion(MemRegion& remote_mr, size_t remote_tail, 
                                             MemRegion& local_mr, size_t local_offset,
                                             size_t sz, ibv_wr_opcode opcode, bool update_remote) {
  if (remote_mr.is_local() || local_mr.is_remote()) {
    rdma_log(RDMA_ERROR, "RDMAConnEvent::rdma_write, MemRegion incorrect");
    exit(-1);
  }

  struct ibv_send_wr sr1, sr2;
  struct ibv_sge sge1, sge2;
  size_t remote_cap = remote_mr.length();
  size_t r_len = MIN(remote_cap - remote_tail, sz);
  
  INIT_SGE(&sge1, (uint8_t*)local_mr.addr() + local_offset, r_len, local_mr.lkey());
  INIT_SR(&sr1, &sge1, opcode, (uint8_t*)remote_mr.addr() + remote_tail, remote_mr.rkey(), 1, r_len);
  rdma_log(RDMA_INFO, "RDMAConn::post_send_and_poll_completion, send %d data to remote %d",
           r_len, remote_tail);
  int ret1 = post_send_and_poll_completion(&sr1, update_remote);

  if (ret1) return ret1;

  if (r_len < sz) {
    INIT_SGE(&sge2, (uint8_t*)local_mr.addr() + local_offset + r_len, sz - r_len, local_mr.lkey());
    INIT_SR(&sr2, &sge2, opcode, remote_mr.addr(), remote_mr.rkey(), 1, sz - r_len);
    rdma_log(RDMA_INFO, "RDMAConn::post_send_and_poll_completion, send %d data to remote %d",
             sz - r_len, 0);
    return post_send_and_poll_completion(&sr2, update_remote);
  } 

  return ret1;
}

