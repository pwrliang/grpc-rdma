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

const size_t DEFAULT_MAX_SEND_WR = 5000;
const size_t DEFAULT_MAX_RECV_WR = 5000;
const size_t DEFAULT_MAX_SEND_SGE = 10;
const size_t DEFAULT_MAX_RECV_SGE = 10;
const size_t DEFAULT_MAX_POST_RECV = 500;

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
    RDMAConn(int fd, RDMANode* node) : fd_(fd), node_(node) {}
    ~RDMAConn();

    virtual void poll_send_completion();
    virtual void post_send_and_poll_completion(ibv_send_wr* sr);
    virtual void post_send_and_poll_completion(MemRegion& remote_mr, size_t remote_tail, 
                                       MemRegion& local_mr, size_t local_offset,
                                       size_t sz, ibv_wr_opcode opcode);
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
    RDMAConnBP(int fd, RDMANode* node);
    virtual ~RDMAConnBP() {}

};

class RDMAConnEvent : public RDMAConn {
    friend class RDMASenderReceiverEvent;
  public:
    RDMAConnEvent(int fd, RDMANode* node, ibv_comp_channel* channel, RDMASenderReceiverEvent* rdmasr);
    virtual ~RDMAConnEvent() {}

    void post_recvs(uint8_t* addr, size_t length, uint32_t lkey, size_t n);
    size_t poll_recv_completions_and_post_recvs(uint8_t* addr, size_t length, uint32_t lkey);
    
  protected:
    ibv_wc recv_wcs_[DEFAULT_MAX_POST_RECV];
};


#endif