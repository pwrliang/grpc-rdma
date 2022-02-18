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
#include "fcntl.h"

// -----< RDMAConnEvent >-----

RDMAConnEvent::RDMAConnEvent(int fd, RDMANode* node, ibv_comp_channel* channel, RDMASenderReceiverEvent* rdmasr)
  : RDMAConn(fd, node) {
  ibv_context* ctx = node_->get_ctx();
  ibv_pd* pd = node_->get_pd();
  ibv_port_attr port_attr = node_->get_port_attr();
  ibv_device_attr dev_attr = node_->get_device_attr();
  union ibv_gid gid = node_->get_gid();

  scq_ = ibv_create_cq(ctx, dev_attr.max_cqe, NULL, NULL, 0);
  if (!scq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConnEvent::RDMAConnEvent, failed to create CQ for a connection");
    exit(-1);
  }
  rcq_ = ibv_create_cq(ctx, dev_attr.max_cqe, rdmasr, channel, 0);
  if (!rcq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConnEvent::RDMAConnEvent, failed to create CQ for a connection");
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

void RDMAConnEvent::post_recvs(uint8_t* addr, size_t length, uint32_t lkey, size_t n = 1) {
  if (n == 0) return;
  struct ibv_recv_wr rr;
  struct ibv_sge sge;
  struct ibv_recv_wr* bad_wr = NULL;

  INIT_SGE(&sge, addr, length, lkey);
  INIT_RR(&rr, &sge);

  int ret;
  while (n--) {
    ret = ibv_post_recv(qp_, &rr, &bad_wr);
    if (ret) {
      rdma_log(RDMA_ERROR, "Failed to post RR, errno = %d, wr_id = %d", ret,
               bad_wr->wr_id);
      exit(-1);
    }
  }
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
  rdma_log(RDMA_DEBUG, "RDMAConnEvent::poll_recv_completions_and_post_recvs, poll out %d completion (%d bytes) from rcq",
           completed, recv_bytes);

  return recv_bytes;
}