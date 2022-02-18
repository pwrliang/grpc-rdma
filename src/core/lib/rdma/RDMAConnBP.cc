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

// -----< RDMAConnBP >-----

RDMAConnBP::RDMAConnBP(int fd, RDMANode* node)
  : RDMAConn(fd, node) {
  ibv_context* ctx = node_->get_ctx();
  ibv_pd* pd = node_->get_pd();
  ibv_port_attr port_attr = node_->get_port_attr();
  ibv_device_attr dev_attr = node_->get_device_attr();
  union ibv_gid gid = node_->get_gid();

  scq_ = ibv_create_cq(ctx, dev_attr.max_cqe, NULL, NULL, 0);
  if (!scq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConnBP::RDMAConnBP, failed to create send CQ");
    exit(-1);
  }
  rcq_ = ibv_create_cq(ctx, dev_attr.max_cqe, NULL, NULL, 0);
  if (!rcq_) {
    rdma_log(RDMA_ERROR,
      "RDMAConnBP::RDMAConnBP, failed to create recv CQ");
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


