#include "./RDMAUtils.h"
#include "RDMASenderReceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
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
  sr->wr_id = id++;
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

// -----< MemRegion >-----

int MemRegion::remote_reg(void* mem, uint32_t rkey, size_t len) {
  dereg();

  remote = true;
  remote_mr.addr = mem;
  remote_mr.rkey = rkey;
  remote_mr.length = len;
  return 0;
}

int MemRegion::local_reg(ibv_pd* pd, void* mem, size_t size, const int f) {
  dereg();

  remote = false;
  ib_pd = pd;
  flag = f;

  local_mr = ibv_reg_mr(ib_pd, mem, size, flag);
  if (!local_mr) {
    rdma_log(RDMA_ERROR,
             "MemRegion::local_reg, failed to register memory region!");
    dereg();
    return -1;
  }

  return 0;
}

void MemRegion::dereg() {
  if (!remote && local_mr)
    if (ibv_dereg_mr(local_mr))
      rdma_log(RDMA_ERROR,
               "MemRegion::local_reg, failed to deregister memory region!");

  local_mr = NULL;
  flag = 0;
  remote = true;
}

// -----< RDMANode >-----

int RDMANode::open(const char* name) {
  ibv_device** dev_list = NULL;
  ibv_device* ib_dev = NULL;
  int num_devices = 0;

  if (!(dev_list = ibv_get_device_list(&num_devices)) || !num_devices) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to get IB device list");
    return -1;
  }

  for (int i = 0; i < num_devices; i++) {
    if (!strcmp(ibv_get_device_name(dev_list[i]), name)) {
      ib_dev = dev_list[i];
      break;
    }
  }

  if (!ib_dev) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to find device \"%s\"", name);
    return -2;
  }

  ib_ctx = ibv_open_device(ib_dev);
  if (!ib_ctx) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to open device %s",
             ibv_get_device_name(ib_dev));
    return -1;
  }
  ibv_free_device_list(dev_list);

  if (ibv_query_port(ib_ctx, ib_port, &port_attr)) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to query port %u attribute",
             ib_port);
    return -1;
  }

  if (ibv_query_gid(ib_ctx, ib_port, 0, &gid)) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to query gid");
    return -1;
  }

  ib_pd = ibv_alloc_pd(ib_ctx);
  if (!ib_pd) {
    rdma_log(RDMA_ERROR, "RDMANode::open, ibv_alloc_pd failed");
    return -1;
  }

  return 0;
}

void RDMANode::event_channel_init() {
  event_channel = ibv_create_comp_channel(ib_ctx);
  int flags = fcntl(event_channel->fd, F_GETFL);
  if (fcntl(event_channel->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    rdma_log(RDMA_ERROR, "Failed to change fd of CQ Event to non-blocking");
  }
}

void RDMANode::close() {
  if (event_channel) {
    ibv_destroy_comp_channel(event_channel);
  }
  if (ib_pd) {
    ibv_dealloc_pd(ib_pd);
    ib_pd = NULL;
  }
  if (ib_ctx) {
    ibv_close_device(ib_ctx);
    ib_ctx = NULL;
  }
  memset(&port_attr, 0, sizeof(port_attr));
}

// -----< RDMAConn >-----

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
  attr.timeout = 0x19;  // previous is 0x12
  attr.retry_cnt = 7;
  attr.rnr_retry = 7;  // previous is 0
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

int RDMAConn::event_poll_completion(ibv_cq* cq, int num_event) {
  ibv_wc wc[num_event];
  int num_entries = ibv_poll_cq(cq, num_event, wc);
  if (num_entries != num_event) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::event_poll_completion, num_entries != num_event, "
             "num_entires = %d, num_event = %d",
             num_entries, num_event);
    return -1;
  }
  int total_recv_bytes = 0;
  for (int i = 0; i < num_entries; i++) {
    if (wc[i].status != IBV_WC_SUCCESS) {
      rdma_log(RDMA_ERROR,
               "RDMAConn::event_poll_completion, wc[%d] status = %d", i,
               wc[i].status);
      // sleep(1);
      return -1;
    }
    int imm_data = wc[i].imm_data;
    total_recv_bytes += imm_data;
  }
  return total_recv_bytes;
}

int RDMAConn::event_check_incoming(int epfd, bool flag) {
  if (flag) {
    struct epoll_event events[MAX_CONNECTIONS];
    int nfds = epoll_wait(epfd, events, MAX_CONNECTIONS, EPOLL_TIMEOUT);

    for (int i = 0; i < nfds; i++) {
      ibv_comp_channel* channel = (ibv_comp_channel*)(events[i].data.ptr);
      ibv_cq* cq;
      void* ev_ctx;
      if (ibv_get_cq_event(
              channel, &cq,
              &ev_ctx)) {  // this may happen if the other side already exit
        rdma_log(
            RDMA_ERROR,
            "fd = %d, RDMAConn::event_check_incoming, failed to get cq event",
            _sd);
        return -1;
      }
      RDMAEventSenderReceiver* rdmasr = (RDMAEventSenderReceiver*)ev_ctx;
      if (ibv_req_notify_cq(cq,
                            0)) {  // this must be called before other thread
                                   // call ibv_get_cq_event with the same cq
        rdma_log(RDMA_ERROR,
                 "fd = %d, RDMAConn::event_check_incoming, failed to notify cq",
                 _sd);
        return -1;
      }
      rdmasr->event_post_recv();
      // ibv_ack_cq_events(cq, 1);

      std::atomic<int>* count = &(rdmasr->conn_->_rcq_event_count);
      int n = count->fetch_add(1, std::memory_order_relaxed);
    }
  }

  int this_count = _rcq_event_count.exchange(0, std::memory_order_relaxed);

  // if (this_count > 1) printf("fd = %d, channel fd = %d has incoming, count =
  // %d\n", _sd, _event_channel->fd, this_count);

  _rcq_unprocessed_event_count.fetch_add(this_count, std::memory_order_relaxed);

  if (this_count > 0) {
    ibv_ack_cq_events(_rcq, this_count);
    return event_poll_completion(_rcq, this_count);
  }

  if (_rcq_unprocessed_event_count.load(std::memory_order_relaxed) > 0)
    return 1;

  return -1;
}

int RDMAConn::event_post_recv(uint8_t* addr, size_t length, uint32_t lkey) {
  struct ibv_recv_wr rr;
  struct ibv_sge sge;
  struct ibv_recv_wr* bad_wr = NULL;

  INIT_SGE(&sge, addr, length, lkey);
  INIT_RR(&rr, &sge);

  int ret = ibv_post_recv(_qp, &rr, &bad_wr);
  if (ret) {
    rdma_log(RDMA_ERROR, "Failed to post RR, errno = %d, wr_id = %d", ret,
             bad_wr->wr_id);
    // sleep(1);
    return 1;
  }

  return 0;
}

int RDMAConn::event_init(int epfd, RDMAEventSenderReceiver* rdmasr) {
  ibv_context* ctx = _node->get_ctx();
  _rdmasr = rdmasr;

  _event_channel = ibv_create_comp_channel(ctx);
  int flags = fcntl(_event_channel->fd, F_GETFL);
  if (fcntl(_event_channel->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    rdma_log(
        RDMA_ERROR,
        "RDMAConn::event_init, failed to change channel fd to non-blocking");
    return 1;
  }
  ibv_cq* cq = ibv_create_cq(ctx, 65, _rdmasr, _event_channel, 0);
  if (!cq) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::event_init, failed to create CQ for a connection");
    return 1;
  }
  _rcq = cq;
  if (init(cq)) return 1;

  struct epoll_event ev;
  ev.events = EPOLLET | EPOLLIN;
  // ev.events = EPOLLET;
  ev.data.fd = _event_channel->fd;
  ev.data.ptr = (void*)_event_channel;
  epoll_ctl(epfd, EPOLL_CTL_ADD, _event_channel->fd, &ev);

  if (ibv_req_notify_cq(_rcq, 0)) {
    rdma_log(RDMA_ERROR, "RDMAConn::event_init, failed to notify rcq");
    return 1;
  }
  return 0;
}

int RDMAConn::init(ibv_cq* rcq, ibv_cq* scq) {
  ibv_context* ctx = _node->get_ctx();
  ibv_pd* pd = _node->get_pd();
  ibv_port_attr port_attr = _node->get_port_attr();
  union ibv_gid gid = _node->get_gid();

  // Create own CQs if no external CQs are given to this connection
  if (rcq == NULL || scq == NULL) {
    ibv_cq* cq = ibv_create_cq(ctx, 65, NULL, NULL, 0);
    if (!cq) {
      rdma_log(RDMA_ERROR,
               "RDMAConn::init, failed to create CQ for a connection");
      return 1;
    }
    if (rcq == NULL) {
      _rcq = rcq = cq;
    }
    if (scq == NULL) {
      _scq = scq = cq;
    }
  }

  // create QP for this connection
  memset(&_qp_attr, 0, sizeof(_qp_attr));

  _qp_attr.recv_cq = rcq;
  _qp_attr.send_cq = scq;
  _qp_attr.qp_type = IBV_QPT_RC;
  _qp_attr.sq_sig_all = 0;

  _qp_attr.cap.max_send_wr = 5000;  // previous is 5000
  _qp_attr.cap.max_recv_wr = 5000;  // previous is 5000
  _qp_attr.cap.max_send_sge =
      10;  // to enable send_msghdr_zerocopy, set big number (10+)
  _qp_attr.cap.max_recv_sge = 1;
  _send_sges = new ibv_sge[_qp_attr.cap.max_send_sge];

  _qp = ibv_create_qp(pd, &_qp_attr);
  if (!_qp) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::init, failed to create QP for a connection");
    return 1;
  }

  struct {
    uint32_t qp_num;
    uint16_t lid;
    uint8_t gid[16];
  } local = {_qp->qp_num, port_attr.lid}, remote;
  memcpy(&local.gid, &gid, sizeof(gid));

  // exchange data for nodes
  if (sync_data((char*)&local, (char*)&remote, sizeof(local))) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::init, failed to exchange QP data and the initial MR");
    return 1;
  }

  _qp_num_rt = remote.qp_num;
  _lid_rt = remote.lid;
  memcpy(&_gid_rt, remote.gid, sizeof(_gid_rt));

  if (modify_state(INIT)) {
    rdma_log(RDMA_ERROR, "RDMAConn::init, failed to change to INIT state");
    return 1;
  }

  if (modify_state(RTR)) {
    rdma_log(RDMA_ERROR, "DMAConn::init, failed to change to RTR state");
    return 1;
  }

  if (modify_state(RTS)) {
    rdma_log(RDMA_ERROR, "DMAConn::init, failed to change to RTS state");
    return 1;
  }

  char tmp;
  if (sync_data((char*)"s", &tmp, 1)) {
    rdma_log(RDMA_ERROR,
             "DMAConn::init, failed to sync after switching QP to RTS");
    return 1;
  }

  return 0;
}

void RDMAConn::clean() {
  if (_rcq && _rcq == _scq) {
    ibv_destroy_cq(_rcq);
  } else {
    if (_rcq) ibv_destroy_cq(_rcq);
    if (_scq) ibv_destroy_cq(_scq);
  }
  _rcq = _scq = NULL;

  if (_qp) {
    ibv_destroy_qp(_qp);
    _qp = NULL;
  }

  if (_send_sges) {
    delete _send_sges;
  }

  if (_event_channel) {
    ibv_destroy_comp_channel(_event_channel);
  }

  _rdmasr = nullptr;
}

int RDMAConn::modify_state(RDMAConn::state st) {
  int ret = 0;

  switch (st) {
    case RESET:
      break;
    case INIT:
      ret = modify_qp_to_init(_qp);
      break;
    case RTR:
      ret = modify_qp_to_rtr(_qp, _qp_num_rt, _lid_rt, _gid_rt,
                             _node->get_port_attr().link_layer);
      break;
    case RTS:
      ret = modify_qp_to_rts(_qp);
      break;
    default:
      rdma_log(RDMA_ERROR, "RDMAConn::modify_state, Unsupported state %d", st);
  }

  return ret;
}

int RDMAConn::sync_data(char* local, char* remote, const size_t sz) {
  size_t remain = sz;
  ssize_t done;
  if (_sd < 3) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::sync_data, failed to sync data with remote, no opened "
             "socket(sd: %d)",
             _sd);
    return -1;
  }

  while (remain) {
    done = ::write(_sd, local + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        // fprintf(stdout, "write in sync_data! %s\n", strerror(errno));
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
    done = ::read(_sd, remote + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        // fprintf(stdout, "read in sync_data! %s\n", strerror(errno));
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

int RDMAConn::poll_completion(ibv_cq* cq) {
  // ibv_wc wc;
  int num_entries;
  do {
    num_entries = ibv_poll_cq(cq, 1, &_wc);
  } while (num_entries == 0);

  if (num_entries < 0) {
    rdma_log(RDMA_ERROR, "fd = %d, RDMAConn::poll_completion return %d", _sd,
             num_entries);
    return 1;
  } else if (_wc.status != IBV_WC_SUCCESS) {
    rdma_log(RDMA_ERROR,
             "fd = %d, RDMAConn::poll_completion return %d with status %d, "
             "vendor syndrome %d, imm count = %d",
             _sd, num_entries, _wc.status, _wc.vendor_err,
             _rdma_write_with_imm_count);
    return 1;
  }
  return 0;
}

int RDMAConn::rdma_write_from_sge(MemRegion& dst, size_t& doff, MemRegion& src,
                                  void* src_addr, size_t sz,
                                  ibv_wr_opcode opcode, bool end_flag) {
  if (!dst.is_remote() || src.is_remote()) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::rdma_write, failed to do RDMA write, src has to be a "
             "local MR and dst has to be a remote MR!");
    return 1;
  }

  size_t dst_cap = dst.length();
  struct ibv_send_wr sr;
  struct ibv_send_wr* bad_wr = NULL;

  for (size_t nwritten = 0, n; nwritten < sz;) {
    n = MIN(dst_cap - doff, sz - nwritten);
    INIT_SGE(&_send_sges[_sge_id++], (char*)src_addr + nwritten, n, src.lkey());
    _nwritten += n;
    nwritten += n;
    if (_sge_id == _qp_attr.cap.max_send_sge || doff + _nwritten == dst_cap ||
        (end_flag && nwritten == sz)) {
      INIT_SR(&sr, _send_sges, opcode, (char*)dst.addr() + doff, dst.rkey(),
              _sge_id);
      doff = (doff + _nwritten) % dst_cap;
      _sge_id = 0;
      _nwritten = 0;
      if (ibv_post_send(_qp, &sr, &bad_wr)) {
        rdma_log(RDMA_ERROR, "RDMAConn::rdma_write, failed to post SR");
        return 1;
      }
      if (poll_completion(_scq)) {
        rdma_log(RDMA_ERROR, "RDMAConn::rdma_write, failed to poll _scq");
        return 1;
      }
    }
    // _nwritten += n;
  }

  return 0;
}

int RDMAConn::rdma_write_event(MemRegion& dst, size_t& doff, MemRegion& src,
                               size_t soff, size_t sz, ibv_wr_opcode opcode,
                               ibv_wr_opcode end_opcode, size_t mlen) {
  if (!dst.is_remote() || src.is_remote()) {
    rdma_log(RDMA_ERROR,
             "fd = %d, RDMAConn::rdma_event_write, failed to do RDMA write, "
             "src has to be a local MR and dst has to be a remote MR!",
             _sd);
    return 1;
  }

  struct ibv_send_wr sr;
  struct ibv_sge sge;
  size_t dst_cap = dst.length();
  size_t original_doff = doff;

  for (size_t nwritten = 0, n; nwritten < sz; nwritten += n) {
    n = MIN(sz - nwritten, dst_cap - doff);
    INIT_SGE(&sge, (char*)src.addr() + soff, n, src.lkey());
    if (n == sz - nwritten) {
      INIT_SR(&sr, &sge, end_opcode, (char*)dst.addr() + doff, dst.rkey(), 1,
              mlen);
      if (end_opcode == IBV_WR_RDMA_WRITE_WITH_IMM) {
        _rdma_write_with_imm_count++;
      }
    } else
      INIT_SR(&sr, &sge, opcode, (char*)dst.addr() + doff, dst.rkey(), 1, mlen);
    if (rdma_write(&sr)) {
      doff = original_doff;
      return 1;
    }
    doff = (doff + n) % dst_cap;
    soff += n;
  }

  return 0;
}

int RDMAConn::rdma_write(MemRegion& dst, size_t& doff, MemRegion& src,
                         size_t soff, size_t sz, ibv_wr_opcode opcode) {
  if (!dst.is_remote() || src.is_remote()) {
    rdma_log(RDMA_ERROR,
             "RDMAConn::rdma_write, failed to do RDMA write, src has to be a "
             "local MR and dst has to be a remote MR!");
    return 1;
  }

  struct ibv_send_wr sr;
  struct ibv_sge sge;
  size_t dst_cap = dst.length();

  for (size_t nwritten = 0, n; nwritten < sz; nwritten += n) {
    n = MIN(sz - nwritten, dst_cap - doff);
    INIT_SGE(&sge, (char*)src.addr() + soff, n, src.lkey());
    INIT_SR(&sr, &sge, opcode, (char*)dst.addr() + doff, dst.rkey());
    // printf("send %d byte to %d, %d\n", n, doff, doff + n);
    if (rdma_write(&sr)) return 1;
    doff = (doff + n) % dst_cap;
    soff += n;
  }

  return 0;
}

int RDMAConn::rdma_write(ibv_send_wr* sr) {
  struct ibv_send_wr* bad_wr = NULL;

  if (ibv_post_send(_qp, sr, &bad_wr)) {
    // rdma_log(RDMA_ERROR, "fd = %d, RDMAConn::rdma_write, failed to post SR",
    //          _sd);
    return 1;
  }
  if (poll_completion(_scq)) {
    // rdma_log(RDMA_ERROR, "fd = %d, RDMAConn::rdma_write, failed to poll _scq",
    //          _sd);
    return 1;
  }

  return 0;
}
