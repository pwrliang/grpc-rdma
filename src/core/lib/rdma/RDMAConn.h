#ifndef _RDMACONN_H_
#define _RDMACONN_H_

#include <infiniband/verbs.h>
#include <unistd.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <pthread.h>
#include <atomic>
#include "RDMAUtils.h"
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>

const size_t DEFAULT_MAX_SEND_WR = 5000;
const size_t DEFAULT_MAX_RECV_WR = 5000;
const size_t DEFAULT_MAX_SEND_SGE = 10;
const size_t DEFAULT_MAX_RECV_SGE = 10;
const size_t DEFAULT_CQE = 5000;
const size_t DEFAULT_MAX_POST_RECV = 500;
const size_t DEFAULT_EVENT_ACK_LIMIT = 100;

class RDMASenderReceiver;
class RDMASenderReceiverBP;
class RDMASenderReceiverEvent;

void INIT_SGE(ibv_sge* sge, void* lc_addr, size_t sz, uint32_t lkey);
void INIT_SR(ibv_send_wr* sr, ibv_sge* sge, ibv_wr_opcode opcode, void* rt_addr,
             uint32_t rkey, int num_sge, uint32_t imm_data);
void INIT_RR(ibv_recv_wr* rr, ibv_sge* sge, int num_sge = 1);

class RDMAConn {
  public:
    typedef enum { UNINIT=0, RESET, INIT, RTR, RTS, SQD, SQE, ERROR } state_t;
    RDMAConn() {}
    RDMAConn(int fd, RDMANode* node);
    ~RDMAConn();

    virtual int poll_send_completion();
    virtual int post_send_and_poll_completion(ibv_send_wr* sr, bool update_remote);
    virtual int post_send_and_poll_completion(MemRegion& remote_mr, size_t remote_tail, 
                                       MemRegion& local_mr, size_t local_offset,
                                       size_t sz, ibv_wr_opcode opcode, bool update_remote);
    // after qp was created, sync data with remote
    virtual int sync();

    int modify_state(state_t st);
    int sync_data(char *local, char *remote, const size_t sz);
    int sync_mr(MemRegion &local, MemRegion &remote);

  protected:
    state_t state_;
    int fd_;
    RDMANode* node_ = nullptr;

    ibv_cq* scq_ = nullptr;
    ibv_cq* rcq_ = nullptr;

    ibv_qp* qp_ = nullptr;
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
    RDMAConnEvent(int fd, RDMANode* node, RDMASenderReceiverEvent* rdmasr);
    virtual ~RDMAConnEvent();

    int get_channel_fd() { return channel_->fd; }
    size_t get_extra_rr_num() { return extra_rr_num_.load(); }
    void reset_extra_rr_num() { extra_rr_num_.store(0); }

    bool get_event_locked();

    size_t get_events_locked(uint8_t* addr, size_t length, uint32_t lkey);

    void post_recvs(uint8_t* addr, size_t length, uint32_t lkey, size_t n);

    // int poll_send_completion();
    size_t poll_recv_completions_and_post_recvs(uint8_t* addr, size_t length, uint32_t lkey);
    
  protected:
    ibv_comp_channel* channel_ = nullptr;
    ibv_wc recv_wcs_[DEFAULT_MAX_POST_RECV];
    size_t unacked_events_num_ = 0;
    
    // this number indicates how many recv requests are posted since last update_remote_metadata
    std::atomic_size_t extra_rr_num_; 
};


#endif