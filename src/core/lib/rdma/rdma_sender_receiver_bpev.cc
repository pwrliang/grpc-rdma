#include <fcntl.h>
#include <infiniband/verbs.h>
#include <sys/eventfd.h>
#include "include/grpcpp/stats_time.h"
#include "src/core/lib/rdma/rdma_poller.h"
#include "src/core/lib/rdma/rdma_sender_receiver.h"

grpc_core::TraceFlag grpc_rdma_sr_bpev_trace(false, "rdma_sr_bpev");
grpc_core::TraceFlag grpc_rdma_sr_bpev_debug_trace(false, "rdma_sr_bpev_debug");

RDMASenderReceiverBPEV::RDMASenderReceiverBPEV(int fd, bool server)
    : RDMASenderReceiverBP(fd, server),
      wakeup_fd_(eventfd(0, EFD_NONBLOCK)),
      index_(0) {}

RDMASenderReceiverBPEV::~RDMASenderReceiverBPEV() {
  RDMAPoller::GetInstance().Unregister(this);
  close(wakeup_fd_);
}

void RDMASenderReceiverBPEV::Init() {
  RDMASenderReceiverBP::Init();
  RDMAPoller::GetInstance().Register(this);
}
