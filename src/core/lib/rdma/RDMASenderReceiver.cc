#include "RDMASenderReceiver.h"
#include "log.h"
#include "fcntl.h"
#include <thread>
// #include <grpc/impl/codegen/log.h>
#define IBV_DEV_NAME "mlx5_0"

// -----< RDMASenderReceiver >-----

std::atomic<bool> RDMASenderReceiver::node_opened_(false);
RDMANode RDMASenderReceiver::node_;

RDMASenderReceiver::RDMASenderReceiver()
  : ringbuf_sz_(DEFAULT_RINGBUF_SZ),
    sendbuf_sz_(DEFAULT_SENDBUF_SZ),
    head_recvbuf_sz_(DEFAULT_HEADBUF_SZ),
    head_sendbuf_sz_(DEFAULT_HEADBUF_SZ) {
  if (!node_opened_.exchange(true)) {
    node_.open(IBV_DEV_NAME);
  }

  write_again_.store(false);

  if (sendbuf_sz_ >= ringbuf_sz_ / 2) {
    sendbuf_sz_ = ringbuf_sz_ / 2 - 1;
    rdma_log(RDMA_WARNING, "RDMASenderReceiver::RDMASenderReceiver, "
             "set sendbuf size to %d", sendbuf_sz_);
  }

  ibv_pd* pd = node_.get_pd();

  sendbuf_ = new uint8_t[sendbuf_sz_];
  if (sendbuf_mr_.local_reg(pd, sendbuf_, sendbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "sendbuf_mr");
    exit(-1);
  }

  posix_memalign(&head_recvbuf_, head_recvbuf_sz_, head_recvbuf_sz_);
  memset(head_recvbuf_, 0, head_recvbuf_sz_);
  if (local_head_recvbuf_mr_.local_reg(pd, head_recvbuf_, head_recvbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "local_head_recvbuf_mr");
             exit(-1);
  }

  posix_memalign(&head_sendbuf_, head_sendbuf_sz_, head_sendbuf_sz_);
  memset(head_sendbuf_, 0, head_sendbuf_sz_);
  if (head_sendbuf_mr_.local_reg(pd, head_sendbuf_, head_sendbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "head_sendbuf_mr");
    exit(-1);
  }
}

RDMASenderReceiver::~RDMASenderReceiver() {
  delete(sendbuf_);
  free(head_recvbuf_);
  free(head_sendbuf_);
  ringbuf_ = nullptr;
  conn_data_ = nullptr;
  if (conn_head_) {
    delete conn_data_;
  }
}

void RDMASenderReceiver::connect(int fd) {
  conn_head_ = new RDMAConn(fd, &node_);
  conn_head_->sync_mr(local_head_recvbuf_mr_, remote_head_recvbuf_mr_);
}

void RDMASenderReceiver::update_remote_head() {
  if (!ringbuf_ || !conn_head_) {
    rdma_log(RDMA_ERROR, "RDMASenderReceiver::update_remote_head, ringbuf or connector has not been initialized");
    exit(-1);
  }

  reinterpret_cast<size_t*>(head_sendbuf_)[0] = ringbuf_->get_head();
  conn_head_->post_send_and_poll_completion(remote_head_recvbuf_mr_, 0,
                                       head_sendbuf_mr_, 0, 
                                       head_sendbuf_sz_, IBV_WR_RDMA_WRITE, true);
  rdma_log(RDMA_INFO, "RDMASenderReceiver::update_remote_head, %d", reinterpret_cast<size_t*>(head_sendbuf_)[0]);
}

void RDMASenderReceiver::update_local_head() {
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(head_recvbuf_)[0];
}
