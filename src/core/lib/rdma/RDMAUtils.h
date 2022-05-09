#ifndef _RDMAUTILS_H_
#define _RDMAUTILS_H_

#include <infiniband/verbs.h>
#include <poll.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#define IBV_DEV_NAME "mlx5_0"
class MemRegion {
 public:
  const static int rw_flag =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
  const static int w_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

  MemRegion() : flag(0), remote(true) {}
  virtual ~MemRegion() { dereg(); }

  // register local_mr from ibv_reg_mr, 0 means successful, -1 means failure.
  int local_reg(std::shared_ptr<ibv_pd> pd, void* mem, size_t size,
                int flag = rw_flag);

  // set remote_mr, return 0.
  int remote_reg(void* mem, uint32_t rkey, size_t len);

  // if it is local_mr, deregister its mr, set remote as true
  void dereg();

  uint32_t rkey() const {
    return remote ? remote_mr.rkey : (local_mr ? local_mr->rkey : 0);
  }
  uint32_t lkey() const {
    return remote ? remote_mr.lkey : (local_mr ? local_mr->lkey : 0);
  }
  void* addr() const {
    return remote ? remote_mr.addr : (local_mr ? local_mr->addr : 0);
  }
  size_t length() const {
    return remote ? remote_mr.length : (local_mr ? local_mr->length : 0);
  }
  bool is_remote() const { return remote; }
  bool is_local() const { return !remote; }

 private:
  std::shared_ptr<ibv_mr> local_mr;  // local memory region
  ibv_mr remote_mr;                  // remote memory region
  int flag;                          // either w_flag or rw_flag

  bool remote;
};

class RDMANode {
 public:
  const static int ib_port = 1;
  RDMANode() { open(IBV_DEV_NAME); }
  ~RDMANode() { close(); }

  static RDMANode& GetInstance() {
    static RDMANode inst;
    return inst;
  }

  std::shared_ptr<ibv_context> get_ctx() const { return ib_ctx; }

  std::shared_ptr<ibv_pd> get_pd() const { return ib_pd; }

  ibv_port_attr get_port_attr() const { return port_attr; }

  union ibv_gid get_gid() const {
    return gid;
  }

  ibv_device_attr get_device_attr() const { return dev_attr; }

 private:
  std::shared_ptr<ibv_context> ib_ctx;
  std::shared_ptr<ibv_pd> ib_pd;
  ibv_port_attr port_attr;
  union ibv_gid gid;
  ibv_device_attr dev_attr;

  void open(const char* name);
  void close();
};

void init_sge(ibv_sge* sge, void* lc_addr, size_t sz, uint32_t lkey);

void init_sr(ibv_send_wr* sr, ibv_sge* sge, ibv_wr_opcode opcode, void* rt_addr,
             uint32_t rkey, int num_sge, uint32_t imm_data, size_t id,
             ibv_send_wr* next);

void init_rr(ibv_recv_wr* rr, ibv_sge* sge, int num_sge);

int modify_qp_to_init(ibv_qp* qp);

int modify_qp_to_rtr(struct ibv_qp* qp, uint32_t remote_qpn, uint16_t dlid,
                     union ibv_gid dgid, uint8_t link_layer);

int modify_qp_to_rts(struct ibv_qp* qp);

int sync_data(int fd, const char* local, char* remote, const size_t sz);

void barrier(int fd);

#endif