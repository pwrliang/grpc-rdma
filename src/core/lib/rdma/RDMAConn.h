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

const size_t DEFAULT_MAX_SEND_WR = 2000;
const size_t DEFAULT_MAX_RECV_WR = 2000;
const size_t DEFAULT_MAX_SEND_SGE = 20; // max is 30
const size_t DEFAULT_MAX_RECV_SGE = 20; // max is 30
const size_t DEFAULT_CQE = 1000;
const size_t DEFAULT_MAX_POST_RECV = 1000;
const size_t DEFAULT_MAX_POST_SEND = 1000;
const size_t DEFAULT_EVENT_ACK_LIMIT = 5000;

class RDMASenderReceiver;
class RDMASenderReceiverBP;
class RDMASenderReceiverEvent;

void INIT_SGE(ibv_sge* sge, void* lc_addr, size_t sz, uint32_t lkey);
void INIT_SR(ibv_send_wr* sr, ibv_sge* sge, ibv_wr_opcode opcode, void* rt_addr,
             uint32_t rkey, int num_sge, uint32_t imm_data, size_t id = 0,
             ibv_send_wr* next = nullptr);
void INIT_RR(ibv_recv_wr* rr, ibv_sge* sge, int num_sge = 1);

class RDMAConn {
 public:
  typedef enum { UNINIT = 0, RESET, INIT, RTR, RTS, SQD, SQE, ERROR } state_t;
  RDMAConn() {}
  RDMAConn(int fd, RDMANode* node);
  virtual ~RDMAConn();

  virtual void poll_send_completion();
  virtual void post_send_and_poll_completion(ibv_send_wr* sr,
                                             bool update_remote);
  virtual int post_send_and_poll_completion(
      MemRegion& remote_mr, size_t remote_tail, MemRegion& local_mr,
      size_t local_offset, size_t sz, ibv_wr_opcode opcode, bool update_remote);
  // after qp was created, sync data with remote
  virtual int sync();

  int modify_state(state_t st);
  int sync_data(char* local, char* remote, const size_t sz);
  int sync_mr(MemRegion& local, MemRegion& remote);

 protected:
  state_t state_;
  int fd_;
  RDMANode* node_;

  std::shared_ptr<ibv_cq> scq_;
  std::shared_ptr<ibv_cq> rcq_;
  std::shared_ptr<ibv_qp> qp_;
  ibv_qp_init_attr qp_attr_;
  uint32_t qp_num_rt_;
  uint16_t lid_rt_;
  union ibv_gid gid_rt_;
};

class RDMAConnBP : public RDMAConn {
 public:
  RDMAConnBP(int fd, RDMANode* node) : RDMAConn(fd, node) {}
  virtual ~RDMAConnBP() {}
};

class RDMAConnEvent : public RDMAConn {
  public:
    friend class RDMASenderReceiverEvent;
    RDMAConnEvent(int fd, RDMANode* node, ibv_comp_channel* recv_channel = nullptr);
    virtual ~RDMAConnEvent();

    // bool get_event_locked();

    size_t get_recv_events_locked(uint8_t* addr, size_t length, uint32_t lkey);

    size_t get_send_events_locked();

  void post_recvs(uint8_t* addr, size_t length, uint32_t lkey, size_t n);

  // int poll_send_completion();
  size_t poll_recv_completions_and_post_recvs(uint8_t* addr, size_t length,
                                              uint32_t lkey);

    int post_send(MemRegion& remote_mr, size_t remote_tail, 
                  MemRegion& local_mr, size_t local_offset,
                  size_t sz, ibv_wr_opcode opcode);
    size_t poll_send_completions();
    
  protected:
    ibv_comp_channel* send_channel_ = nullptr;
    ibv_comp_channel* recv_channel_ = nullptr;
    ibv_wc recv_wcs_[DEFAULT_MAX_POST_RECV];
    ibv_wc send_wcs_[DEFAULT_MAX_POST_SEND];

    ibv_recv_wr recv_wrs_[DEFAULT_MAX_POST_RECV];
    ibv_sge recv_sges_[DEFAULT_MAX_POST_RECV];
    size_t rr_tail_ = 0, rr_garbage_ = 0;
    size_t unacked_recv_events_num_ = 0;

    // ibv_recv_wr recv_wrs_[DEFAULT_MAX_POST_RECV];
    // ibv_sge recv_sges_[DEFAULT_MAX_POST_SEND];
    // size_t rr_tail_ = 0, rr_garbage_ = 0;

    ibv_send_wr send_wrs_[DEFAULT_MAX_POST_SEND];
    ibv_sge send_sges_[DEFAULT_MAX_POST_SEND];
    size_t sr_tail_ = 0, sr_head_ = 0;
    size_t unacked_send_events_num_ = 0;
};

#endif