#ifndef _RDMAUTILS_H_
#define _RDMAUTILS_H_

#include <infiniband/verbs.h>
#include <unistd.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <pthread.h>
#include <atomic>

const size_t DEFAULT_MAX_SEND_WR = 5000;
const size_t DEFAULT_MAX_RECV_WR = 5000;
const size_t DEFAULT_MAX_SEND_SGE = 10;
const size_t DEFAULT_MAX_RECV_SGE = 10;
const size_t DEFAULT_MAX_POST_RECV = 500;
const size_t DEFAULT_MAX_CQE = 100;

class RDMASenderReceiver;
class RDMASenderReceiverBP;
class RDMASenderReceiverEvent;

class MemRegion {
  public:
    const static int rw_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    const static int w_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

    MemRegion() : local_mr(NULL), flag(0), remote(true) {}
    virtual ~MemRegion() { dereg(); }


    // register local_mr from ibv_reg_mr, 0 means successful, -1 means failure.
    int local_reg(ibv_pd *pd, void *mem, size_t size, int flag = rw_flag);

    // set remote_mr, return 0.
    int remote_reg(void *mem, uint32_t rkey, size_t len);

    // if it is local_mr, deregister its mr, set remote as true
    void dereg();

    uint32_t rkey() const { return remote ? remote_mr.rkey : (local_mr ? local_mr->rkey : 0); }
    uint32_t lkey() const { return remote ? remote_mr.lkey : (local_mr ? local_mr->lkey : 0); }
    void* addr() const { return remote ? remote_mr.addr : (local_mr ? local_mr->addr : 0); }
    size_t length() const { return remote ? remote_mr.length : (local_mr ? local_mr->length : 0); }
    bool is_remote() const { return remote; }
    bool is_local() const { return !remote; }

  private:
    ibv_pd *ib_pd; // protection domain
    ibv_mr *local_mr; // local memory region
    ibv_mr remote_mr; // remote memory region
    int flag; // either w_flag or rw_flag

    bool remote;
};

class RDMANode {
  public:
    const static int ib_port = 1;
    RDMANode() : ib_ctx(NULL), ib_pd(NULL) {}
    virtual ~RDMANode() { close(); }

    int open(const char* name);
    void close();

    ibv_context *get_ctx() const { return ib_ctx; }
    ibv_pd *get_pd() const { return ib_pd; }
    ibv_port_attr get_port_attr() const { return port_attr; }
    union ibv_gid get_gid() const { return gid; }
    ibv_device_attr get_device_attr() const { return dev_attr; }
    
  private:
    ibv_context *ib_ctx;
    ibv_pd *ib_pd;
    ibv_port_attr port_attr;
    union ibv_gid gid;
    ibv_device_attr dev_attr;
};

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
    virtual int init();

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