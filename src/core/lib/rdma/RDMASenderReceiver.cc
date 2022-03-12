#include "RDMASenderReceiver.h"
#include "log.h"
#include "fcntl.h"
#include <thread>
// #include <grpc/impl/codegen/log.h>
#define IBV_DEV_NAME "mlx5_0"

// -----< RDMASenderReceiver >-----

std::atomic_bool RDMASenderReceiver::node_opened_(false);
std::atomic_bool RDMASenderReceiver::node_opened_done_(false);
RDMANode RDMASenderReceiver::node_;

RDMASenderReceiver::RDMASenderReceiver()
  : ringbuf_sz_(DEFAULT_RINGBUF_SZ),
    sendbuf_sz_(DEFAULT_SENDBUF_SZ),
    metadata_recvbuf_sz_(DEFAULT_HEADBUF_SZ),
    metadata_sendbuf_sz_(DEFAULT_HEADBUF_SZ) {
  if (!node_opened_.exchange(true)) {
    node_.open(IBV_DEV_NAME);
    node_opened_done_.store(true);
  } else {
    while (!node_opened_done_.load()) {}
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

  posix_memalign(&metadata_recvbuf_, metadata_recvbuf_sz_, metadata_recvbuf_sz_);
  memset(metadata_recvbuf_, 0, metadata_recvbuf_sz_);
  if (local_metadata_recvbuf_mr_.local_reg(pd, metadata_recvbuf_, metadata_recvbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "local_metadata_recvbuf_mr");
             exit(-1);
  }

  posix_memalign(&metadata_sendbuf_, metadata_sendbuf_sz_, metadata_sendbuf_sz_);
  memset(metadata_sendbuf_, 0, metadata_sendbuf_sz_);
  if (metadata_sendbuf_mr_.local_reg(pd, metadata_sendbuf_, metadata_sendbuf_sz_)) {
    rdma_log(RDMA_ERROR,
             "RDMASenderReceiver::RDMASenderReceiver, failed to local_reg "
             "metadata_sendbuf_mr");
    exit(-1);
  }
}

RDMASenderReceiver::~RDMASenderReceiver() {
  delete[] sendbuf_;
  free(metadata_recvbuf_);
  free(metadata_sendbuf_);
  ringbuf_ = nullptr;
  conn_data_ = nullptr;
  delete conn_metadata_;
}

void RDMASenderReceiver::connect(int fd) {
  conn_metadata_ = new RDMAConn(fd, &node_);
  conn_metadata_->sync_mr(local_metadata_recvbuf_mr_, remote_metadata_recvbuf_mr_);
}

void RDMASenderReceiver::update_remote_metadata() {
  if (!ringbuf_ || !conn_metadata_) {
    rdma_log(RDMA_ERROR, "RDMASenderReceiver::update_remote_metadata, ringbuf or connector has not been initialized");
    exit(-1);
  }

  reinterpret_cast<size_t*>(metadata_sendbuf_)[0] = ringbuf_->get_head();
  conn_metadata_->post_send_and_poll_completion(remote_metadata_recvbuf_mr_, 0,
                                       metadata_sendbuf_mr_, 0,
                                       metadata_sendbuf_sz_, IBV_WR_RDMA_WRITE, true);
  rdma_log(RDMA_INFO, "RDMASenderReceiver::update_remote_metadata, %d", reinterpret_cast<size_t*>(metadata_sendbuf_)[0]);
}

void RDMASenderReceiver::update_local_metadata() {
  remote_ringbuf_head_ = reinterpret_cast<size_t*>(metadata_recvbuf_)[0];
}
