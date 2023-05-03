#include "rdma_utils.h"
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include "grpc/impl/codegen/log.h"
#include "ringbuffer.h"

int MemRegion::RegisterRemote(void* mem, uint32_t rkey, size_t len) {
  dereg();

  remote = true;
  remote_mr.addr = mem;
  remote_mr.rkey = rkey;
  remote_mr.length = len;
  return 0;
}

int MemRegion::RegisterLocal(std::shared_ptr<ibv_pd> pd, void* mem, size_t size,
                             int flag) {
  dereg();

  remote = false;

  local_mr = std::shared_ptr<ibv_mr>(
      ibv_reg_mr(pd.get(), mem, size, flag), [](ibv_mr* p) {
        if (ibv_dereg_mr(p)) {
          gpr_log(
              GPR_ERROR,
              "MemRegion::RegisterLocal, failed to deregister memory region!");
        }
      });
  if (!local_mr) {
    gpr_log(GPR_ERROR,
            "MemRegion::RegisterLocal, failed to register memory region!");
    dereg();
    return -1;
  }

  return 0;
}

void MemRegion::dereg() {
  if (!remote) {
    local_mr.reset();
  }
  remote = true;
}

void RDMANode::open(const char* name) {
  ibv_device** dev_list;
  ibv_device* ib_dev = nullptr;
  int num_devices = 0;

  if (!(dev_list = ibv_get_device_list(&num_devices)) || !num_devices) {
    gpr_log(GPR_ERROR, "RDMANode::open, failed to get IB device list");
    abort();
  }

  for (int i = 0; i < num_devices; i++) {
    if (!strcmp(ibv_get_device_name(dev_list[i]), name)) {
      ib_dev = dev_list[i];
      break;
    }
  }

  if (!ib_dev) {
    gpr_log(GPR_ERROR, "RDMANode::open, failed to find device \"%s\"", name);
    abort();
  }

  ib_ctx = std::shared_ptr<ibv_context>(
      ibv_open_device(ib_dev), [](ibv_context* p) { ibv_close_device(p); });
  if (ib_ctx == nullptr) {
    gpr_log(GPR_ERROR, "RDMANode::open, failed to open device %s",
            ibv_get_device_name(ib_dev));
    abort();
  }
  ibv_free_device_list(dev_list);

  if (ibv_query_port(ib_ctx.get(), ib_port, &port_attr)) {
    gpr_log(GPR_ERROR, "RDMANode::open, failed to query port %u attribute",
            ib_port);
    abort();
  }

  if (ibv_query_gid(ib_ctx.get(), ib_port, 0, &gid)) {
    gpr_log(GPR_ERROR, "RDMANode::open, failed to query gid");
    abort();
  }

  if (ibv_query_device(ib_ctx.get(), &dev_attr)) {
    gpr_log(GPR_ERROR, "RDMANode::open, failed to query device");
    abort();
  }

  //  gpr_log(GPR_INFO,
  //          "device %s attribute: max_cqe = %d, max_qp_wr = %d, max_sge = %d",
  //          name, dev_attr.max_cqe, dev_attr.max_qp_wr, dev_attr.max_sge);

  ib_pd = std::shared_ptr<ibv_pd>(ibv_alloc_pd(ib_ctx.get()),
                                  [](ibv_pd* p) { ibv_dealloc_pd(p); });
  if (ib_pd == nullptr) {
    gpr_log(GPR_ERROR, "RDMANode::open, ibv_alloc_pd failed");
    abort();
  }
}

void RDMANode::close() {
  memset(&port_attr, 0, sizeof(port_attr));
}

void init_sge(ibv_sge* sge, void* lc_addr, size_t sz, uint32_t lkey) {
  memset(sge, 0, sizeof(ibv_sge));
  sge->addr = (uint64_t)lc_addr;
  sge->length = sz;
  sge->lkey = lkey;
}

void init_sr(ibv_send_wr* sr, ibv_sge* sge, ibv_wr_opcode opcode, void* rt_addr,
             uint32_t rkey, int num_sge, uint32_t imm_data, size_t id,
             ibv_send_wr* next) {
  memset(sr, 0, sizeof(ibv_send_wr));
  sr->next = next;
  sr->wr_id = id;
  sr->sg_list = sge;
  sr->num_sge = num_sge;
  sr->opcode = opcode;
  if (imm_data > 0) {
    sr->imm_data = imm_data;
  }
  sr->send_flags = IBV_SEND_SIGNALED;
  sr->wr.rdma.remote_addr = (uint64_t)rt_addr;
  sr->wr.rdma.rkey = rkey;
}

void init_rr(ibv_recv_wr* rr, ibv_sge* sge, int num_sge) {
  static int id = 0;
  memset(rr, 0, sizeof(ibv_recv_wr));
  rr->next = NULL;
  rr->wr_id = id++;
  rr->sg_list = sge;
  rr->num_sge = num_sge;
}

int modify_qp_to_init(ibv_qp* qp) {
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
  if (rc) {
    gpr_log(GPR_ERROR,
            "modify_qp_to_init, failed to modify QP state to INIT, errno: %d",
            errno);
  }
  return rc;
}

int modify_qp_to_rtr(struct ibv_qp* qp, uint32_t remote_qpn,
                     uint32_t remote_psn, uint16_t dlid, union ibv_gid dgid,
                     uint8_t link_layer) {
  struct ibv_qp_attr attr;
  int flags;
  int rc;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = IBV_MTU_1024;  // previous is IBV_MTU_1024
  attr.dest_qp_num = remote_qpn;
  attr.rq_psn = remote_psn;
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
    gpr_log(GPR_ERROR,
            "modify_qp_to_rtr, failed to modify QP state to RTR (%d)", rc);
  }
  return rc;
}

int modify_qp_to_rts(struct ibv_qp* qp, uint32_t sq_psn) {
  struct ibv_qp_attr attr;
  int flags;
  int rc;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTS;
  attr.timeout = 0x14;  // previous is 0x12
  attr.retry_cnt = 6;
  attr.rnr_retry = 0;  // previous is 0
  attr.sq_psn = sq_psn;
  attr.max_rd_atomic = 1;
  flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
          IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
  rc = ibv_modify_qp(qp, &attr, flags);
  if (rc) {
    gpr_log(GPR_ERROR, "modify_qp_to_rts, failed to modify QP state to RTS\n");
  }
  return rc;
}

int sync_data(int fd, const char* local, char* remote, const size_t sz) {
  size_t remain = sz;
  ssize_t done;
  if (fd < 3) {
    gpr_log(GPR_ERROR,
            "RDMAConn::sync_data, failed to sync data with remote, no opened "
            "socket(sd: %d)",
            fd);
    return -1;
  }

  while (remain) {
    done = ::write(fd, local + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
      } else {
        gpr_log(GPR_ERROR, "RDMAConn::sync_data, write errno %d: %s", errno,
                strerror(errno));
        return -1;
      }
    } else {
      remain -= done;
    }
  }

  remain = sz;
  while (remain) {
    done = ::read(fd, remote + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
      } else {
        gpr_log(GPR_ERROR, "RDMAConn::sync_data, read errno %d: %s", errno,
                strerror(errno));
        return -1;
      }
    } else {
      remain -= done;
    }
  }

  return 0;
}

void barrier(int fd) {
  const char* data = "s";
  char tmp;
  if (sync_data(fd, data, &tmp, 1)) {
    gpr_log(GPR_ERROR, "Send data failed");
    abort();
  }
}

/* helper function to print the content of the async event */
void print_async_event(struct ibv_context* ctx, struct ibv_async_event* event) {
  switch (event->event_type) {
    /* QP events */
    case IBV_EVENT_QP_FATAL:
      gpr_log(GPR_INFO, "QP fatal event for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_QP_REQ_ERR:
      gpr_log(GPR_INFO, "QP Requestor error for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_QP_ACCESS_ERR:
      gpr_log(GPR_INFO, "QP access error event for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_COMM_EST:
      gpr_log(GPR_INFO,
              "QP communication established event for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_SQ_DRAINED:
      gpr_log(GPR_INFO, "QP Send Queue drained event for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_PATH_MIG:
      gpr_log(GPR_INFO, "QP Path migration loaded event for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_PATH_MIG_ERR:
      gpr_log(GPR_INFO, "QP Path migration error event for QP with handle %p",
              event->element.qp);
      break;
    case IBV_EVENT_QP_LAST_WQE_REACHED:
      gpr_log(GPR_INFO, "QP last WQE reached event for QP with handle %p",
              event->element.qp);
      break;

    /* CQ events */
    case IBV_EVENT_CQ_ERR:
      gpr_log(GPR_INFO, "CQ error for CQ with handle %p", event->element.cq);
      break;

    /* SRQ events */
    case IBV_EVENT_SRQ_ERR:
      gpr_log(GPR_INFO, "SRQ error for SRQ with handle %p", event->element.srq);
      break;
    case IBV_EVENT_SRQ_LIMIT_REACHED:
      gpr_log(GPR_INFO, "SRQ limit reached event for SRQ with handle %p",
              event->element.srq);
      break;

    /* Port events */
    case IBV_EVENT_PORT_ACTIVE:
      gpr_log(GPR_INFO, "Port active event for port number %d",
              event->element.port_num);
      break;
    case IBV_EVENT_PORT_ERR:
      gpr_log(GPR_INFO, "Port error event for port number %d",
              event->element.port_num);
      break;
    case IBV_EVENT_LID_CHANGE:
      gpr_log(GPR_INFO, "LID change event for port number %d",
              event->element.port_num);
      break;
    case IBV_EVENT_PKEY_CHANGE:
      gpr_log(GPR_INFO, "P_Key table change event for port number %d",
              event->element.port_num);
      break;
    case IBV_EVENT_GID_CHANGE:
      gpr_log(GPR_INFO, "GID table change event for port number %d",
              event->element.port_num);
      break;
    case IBV_EVENT_SM_CHANGE:
      gpr_log(GPR_INFO, "SM change event for port number %d",
              event->element.port_num);
      break;
    case IBV_EVENT_CLIENT_REREGISTER:
      gpr_log(GPR_INFO, "Client reregister event for port number %d",
              event->element.port_num);
      break;

    /* RDMA device events */
    case IBV_EVENT_DEVICE_FATAL:
      gpr_log(GPR_INFO, "Fatal error event for device %s",
              ibv_get_device_name(ctx->device));
      break;
    default:
      gpr_log(GPR_INFO, "Unknown event (%d)", event->event_type);
  }
}
