#ifndef _RDMACONN_H_
#define _RDMACONN_H_

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
#include "RDMAUtils.h"
#include "grpc/impl/codegen/log.h"
#define DEFAULT_MAX_SEND_WR 512
#define DEFAULT_MAX_RECV_WR 512
#define DEFAULT_MAX_SEND_SGE 20  // max is 30
#define DEFAULT_MAX_RECV_SGE 20  // max is 30
#define DEFAULT_CQE 1024
#define DEFAULT_MAX_POST_RECV 512
#define DEFAULT_MAX_POST_SEND 512
#define DEFAULT_EVENT_ACK_LIMIT 512

class RDMASenderReceiver;
class RDMASenderReceiverEvent;

class RDMAConn {
 public:
  typedef enum { UNINIT = 0, RESET, INIT, RTR, RTS, SQD, SQE, ERROR } state_t;
  explicit RDMAConn(RDMANode* node, bool event_mode = false);
  virtual ~RDMAConn();

  int poll_send_completion(int expected_num_entries);

  int post_send(MemRegion& remote_mr, size_t remote_tail, MemRegion& local_mr,
                size_t local_offset, size_t sz, ibv_wr_opcode opcode);
  
  int post_sends(MemRegion& remote_mr, size_t remote_tail,
                 struct ibv_sge* sg_list, size_t num_sge, size_t sz, ibv_wr_opcode opcode);

  size_t get_recv_events_locked();

  /**
   * Post n receive requests
   * @param n the number of requests
   * @return return True if
   */
  void post_recvs(size_t n);

  // after qp was created, sync data with remote
  int SyncQP(int fd);
  int SyncMR(int fd, MemRegion& local, MemRegion& remote);
  void Sync(int fd);

  int get_recv_channel_fd() const { return recv_channel_->fd; }

  size_t get_rr_tail() const { return rr_tail_; }

  size_t get_rr_garbage() const { return rr_garbage_; }

  void set_rr_garbage(size_t rr_garbage) { rr_garbage_ = rr_garbage; }

  /**
   * Post receive requests only when the number of finished receive requests
   * exceeded a threshold
   * @return return true if post receive happened
   */
  bool post_recvs_lazy() {
    if (rr_garbage_ < DEFAULT_MAX_POST_RECV / 2) {
      return false;
    }
    post_recvs(rr_garbage_);
    rr_garbage_ = 0;
    return true;
  }

 private:
  state_t state_;
  RDMANode* node_;

  std::shared_ptr<ibv_cq> scq_;
  std::shared_ptr<ibv_cq> rcq_;
  std::shared_ptr<ibv_qp> qp_;
  ibv_qp_init_attr qp_attr_;
  uint32_t qp_num_rt_;
  uint16_t lid_rt_;
  union ibv_gid gid_rt_;

  std::shared_ptr<ibv_comp_channel> recv_channel_;
  // rr stands for receive request
  size_t rr_tail_ = 0, rr_garbage_ = 0;
  size_t unacked_recv_events_num_ = 0;

  int modify_state(state_t st) {
    int ret = 0;

    switch (st) {
      case RESET:
        break;
      case INIT:
        ret = modify_qp_to_init(qp_.get());
        break;
      case RTR:
        ret = modify_qp_to_rtr(qp_.get(), qp_num_rt_, lid_rt_, gid_rt_,
                               node_->get_port_attr().link_layer);
        break;
      case RTS:
        ret = modify_qp_to_rts(qp_.get());
        break;
      default:
        gpr_log(GPR_ERROR, "RDMAConn::modify_state, Unsupported state %d", st);
        exit(1);
    }

    return ret;
  }
  size_t poll_recv_completion();
};

#endif