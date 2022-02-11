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
#include "log.h"
// #include "RDMASenderReceiver.h"

#define MAX_CONNECTIONS 200
#define EPOLL_TIMEOUT 1

class RDMAEventSenderReceiver;

void INIT_SGE(ibv_sge *sge, void *lc_addr, size_t sz, uint32_t lkey);
void INIT_SR(ibv_send_wr *sr, ibv_sge *sge, ibv_wr_opcode opcode, void *rt_addr, uint32_t rkey, int num_sge = 1, uint32_t imm_data = 0);
void INIT_RR(ibv_recv_wr *rr, ibv_sge *sge, int num_sge = 1);

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

    ibv_pd *ib_pd; // protection domain
    ibv_mr *local_mr; // local memory region
    ibv_mr remote_mr; // remote memory region
    int flag; // either w_flag or rw_flag

    bool remote;
};

class RDMANode {
public:
    const static int ib_port = 1;

    RDMANode() : ib_ctx(NULL), ib_pd(NULL) { SET_RDMA_VERBOSITY(RDMA_ENV_VAR); }
    virtual ~RDMANode() { close(); }

    int open(const char* name);
    void close();
    ibv_context *get_ctx() const { return ib_ctx; }
    ibv_pd *get_pd() const { return ib_pd; }
    ibv_port_attr get_port_attr() const { return port_attr; }
    union ibv_gid get_gid() const { return gid; }

    ibv_context *ib_ctx;
    ibv_pd *ib_pd;
    ibv_port_attr port_attr;
    union ibv_gid gid;

    void event_channel_init();
    ibv_comp_channel *event_channel = nullptr;
};


class RDMAConn {
public:
    typedef enum { UNINIT=0, RESET, INIT, RTR, RTS, SQD, SQE, ERROR } state;

    RDMAConn(int sd, RDMANode *node) : 
        _state(UNINIT), _sd(sd), _node(node), _rcq(NULL), _scq(NULL), _qp(NULL) {}
    virtual ~RDMAConn() { clean(); }

    /*
     * create scq and rcq if not provide. 
     * Set qp_attr and create _qp from ibv_create_qp.
     * Set local qp_num, lid and gid, then sync these data with remote.
     * modify_state through INIT, RTR, RTS.Then sync().
     */
    int init(ibv_cq *rcq = NULL, ibv_cq *scq = NULL);

    // ibv_destroy_cq, ibv_destroy_qp
    void clean();

    int modify_state(state st);

    // sync data (qp_num, lid, gid) with socket
    int sync_data(char *local, char *remote, const size_t sz);

    // sync mr data (addr, rkey, length with remote, and register remote_mr)
    int sync_mr(MemRegion &local, MemRegion &remote);

    // poll scq, 0 success, 1 error
    virtual int poll_completion(ibv_cq *cq);

    // init one sge from src + soff, then init sr, then ibv_post_send and poll_completion, 0 success, 1 error
    int rdma_write(MemRegion &dst, size_t &doff, MemRegion &src, size_t soff, size_t sz, ibv_wr_opcode opcode);
    int rdma_write(ibv_send_wr *sr);
    int rdma_write_event(MemRegion &dst, size_t &doff, MemRegion &src, size_t soff, size_t sz, ibv_wr_opcode opcode, ibv_wr_opcode end_opcode, size_t mlen);
    int rdma_write_from_sge(MemRegion &dst, size_t &doff, MemRegion &src, void *src_addr, size_t sz, ibv_wr_opcode opcode, bool end_flag);

    state _state;
    int _sd; // a connected fd, usually connected by tcp socket
    RDMAEventSenderReceiver *_rdmasr = nullptr;
    RDMANode *_node;
    ibv_cq *_rcq;
    ibv_cq *_scq;
    ibv_qp_init_attr _qp_attr;
    ibv_qp *_qp;
    uint32_t _qp_num_rt;  /* local: _qp->qp_num */
    uint16_t _lid_rt;     /* local: host->get_port_attr().lid */
    union ibv_gid _gid_rt;

    ibv_sge *_send_sges = nullptr;
    size_t _sge_id = 0;
    size_t _nwritten = 0;

    // event
    bool _one_check_all_flag = true;
    ibv_comp_channel *_event_channel = nullptr;
    std::atomic<int> _rcq_event_count{0};
    // int _rcq_unprocessed_event_count = 0;
    std::atomic<int> _rcq_unprocessed_event_count{0};
    ibv_wc _wc;
    int _rdma_write_with_imm_count = 0;

    int event_init(int epfd, RDMAEventSenderReceiver *rdmasr);
    int event_post_recv(uint8_t* addr, size_t length, uint32_t lkey);
    int event_check_incoming(int epfd, bool flag);
    int event_poll_completion(ibv_cq *cq, int num_event);
};

#endif