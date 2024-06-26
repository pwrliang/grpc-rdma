#ifndef GRPC_CORE_LIB_RDMA_RDMA_CONN_H
#define GRPC_CORE_LIB_RDMA_RDMA_CONN_H

#include <infiniband/verbs.h>
#include <poll.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include "grpc/impl/codegen/log.h"
#include "src/core/lib/rdma/rdma_utils.h"
#define DEFAULT_MIN_RESERVED_WR 10
#define DEFAULT_MAX_SEND_WR 512
#define DEFAULT_MAX_RECV_WR 512
#define DEFAULT_MAX_SEND_SGE 30  // max is 30
#define DEFAULT_MAX_RECV_SGE 30  // max is 30
#define DEFAULT_CQE 1024 // default is 1024
#define DEFAULT_MAX_POST_RECV 512 // default is 512
#define DEFAULT_MAX_POST_SEND 512 // default is 512
#define DEFAULT_EVENT_ACK_LIMIT 512 // default is 512

class RDMASenderReceiver;
class RDMASenderReceiverEvent;

class RDMAConn {
 public:
  explicit RDMAConn(int fd, RDMANode* node, const char* name = "",
                    bool event_mode = false);
  virtual ~RDMAConn();

  int PollSendCompletion(int expected_num_entries);

  int PollSendCompletion();

  int PostSendRequest(MemRegion& remote_mr, MemRegion& local_mr, size_t sz,
                      ibv_wr_opcode opcode);

  int PostSendRequest(MemRegion& remote_mr, size_t remote_tail,
                      MemRegion& local_mr, size_t local_offset, size_t sz,
                      ibv_wr_opcode opcode);

  int PostSendRequests(MemRegion& remote_mr, size_t remote_tail,
                       struct ibv_sge* sg_list, size_t num_sge, size_t sz,
                       ibv_wr_opcode opcode);

  size_t GetRecvEvents();

  /**
   * Post n receive requests
   * @param n the number of requests
   * @return return True if
   */
  void PostRecvRequests(size_t n);

  // after qp was created, sync data with remote
  int SyncQP();

  int SyncMR(const MemRegion& local, MemRegion& remote);

  void Sync() { barrier(fd_); }

  int get_recv_channel_fd() const { return recv_channel_->fd; }

  size_t get_rr_tail() const { return rr_tail_; }

  size_t get_rr_garbage() const { return rr_garbage_; }

  void set_rr_garbage(size_t rr_garbage) { rr_garbage_ = rr_garbage; }

  /**
   * Post receive requests only when the number of finished receive requests
   * exceeded a threshold
   * @return return true if post receive happened
   */
  bool PostRecvRequestsLazy() {
    if (rr_garbage_ >= DEFAULT_MAX_POST_RECV / 2) {
      PostRecvRequests(rr_garbage_);
      rr_garbage_ = 0;
      return true;
    }
    return false;
  }

  bool IsPeerAlive() const {
    uint8_t buf;
    int ret;

    do {
      ret = recv(fd_, &buf, 0, 0);
    } while (ret < 0 && errno == EINTR);

    return ret != 0;
  }

 private:
  size_t PollRecvCompletion();

  int fd_;
  RDMANode* node_;

  std::shared_ptr<ibv_cq> scq_;
  std::shared_ptr<ibv_cq> rcq_;
  std::shared_ptr<ibv_qp> qp_;
  ibv_qp_init_attr qp_attr_;
  uint32_t qp_num_rt_;
  uint16_t lid_rt_;
  uint32_t psn_rt_;
  union ibv_gid gid_rt_;

  std::shared_ptr<ibv_comp_channel> recv_channel_;
  const char* name_;
  // rr stands for receive request
  size_t rr_tail_ = 0, rr_garbage_ = 0;
  size_t unack_cqe_ = 0;
};

#endif  // GRPC_CORE_LIB_RDMA_RDMA_CONN_H