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
#include <thread>
#include <condition_variable>
#include <mutex>

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


class TimerPackage {
  public:
    TimerPackage(size_t timeout_ms = 10000);
    virtual ~TimerPackage();
    void Start();
    void Start(const char *format, ...);
    void BlockIfTimeout();
    void Stop();
    void SetTimeout(size_t timeout_ms);

private:
  static std::atomic_size_t global_count;
  const size_t local_id;
  std::atomic_size_t timeout_ms_;
  std::condition_variable timer_;
  std::mutex timer_mu_;
  std::atomic_bool alive_;
  std::condition_variable start_;
  std::mutex start_mu_;
  std::atomic_bool timeout_flag_;
  std::thread* thread_;
  char* message_ = nullptr;
};


#endif