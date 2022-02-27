#include "RDMAUtils.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "fcntl.h"

// -----< MemRegion >-----

int MemRegion::remote_reg(void* mem, uint32_t rkey, size_t len) {
  dereg();

  remote = true;
  remote_mr.addr = mem;
  remote_mr.rkey = rkey;
  remote_mr.length = len;
  return 0;
}

int MemRegion::local_reg(ibv_pd* pd, void* mem, size_t size, const int f) {
  dereg();

  remote = false;
  ib_pd = pd;
  flag = f;

  local_mr = ibv_reg_mr(ib_pd, mem, size, flag);
  if (!local_mr) {
    rdma_log(RDMA_ERROR,
             "MemRegion::local_reg, failed to register memory region!");
    dereg();
    return -1;
  }

  return 0;
}

void MemRegion::dereg() {
  if (!remote && local_mr)
    if (ibv_dereg_mr(local_mr))
      rdma_log(RDMA_ERROR,
               "MemRegion::local_reg, failed to deregister memory region!");

  local_mr = NULL;
  flag = 0;
  remote = true;
}


// -----< RDMANode >-----

int RDMANode::open(const char* name) {
  ibv_device** dev_list = NULL;
  ibv_device* ib_dev = NULL;
  int num_devices = 0;

  if (!(dev_list = ibv_get_device_list(&num_devices)) || !num_devices) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to get IB device list");
    return -1;
  }

  for (int i = 0; i < num_devices; i++) {
    if (!strcmp(ibv_get_device_name(dev_list[i]), name)) {
      ib_dev = dev_list[i];
      break;
    }
  }

  if (!ib_dev) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to find device \"%s\"", name);
    return -2;
  }

  ib_ctx = ibv_open_device(ib_dev);
  if (!ib_ctx) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to open device %s",
             ibv_get_device_name(ib_dev));
    return -1;
  }
  ibv_free_device_list(dev_list);

  if (ibv_query_port(ib_ctx, ib_port, &port_attr)) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to query port %u attribute",
             ib_port);
    return -1;
  }

  if (ibv_query_gid(ib_ctx, ib_port, 0, &gid)) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to query gid");
    return -1;
  }

  if (ibv_query_device(ib_ctx, &dev_attr)) {
    rdma_log(RDMA_ERROR, "RDMANode::open, failed to query device");
    return -1;
  }

  ib_pd = ibv_alloc_pd(ib_ctx);
  if (!ib_pd) {
    rdma_log(RDMA_ERROR, "RDMANode::open, ibv_alloc_pd failed");
    return -1;
  }

  SET_RDMA_VERBOSITY();

  return 0;
}

void RDMANode::close() {
  if (ib_pd) {
    ibv_dealloc_pd(ib_pd);
    ib_pd = NULL;
  }
  if (ib_ctx) {
    ibv_close_device(ib_ctx);
    ib_ctx = NULL;
  }
  memset(&port_attr, 0, sizeof(port_attr));
}


// -----< RDMATimer >-----


void RDMATimer::RDMATimerFunc(RDMATimer* args) {
  std::unique_lock<std::mutex> lck(args->mtx_);

  while (true) {
    
    args->start_.wait(lck);

    if (!args->alive_) break;

    if (args->timer_.wait_for(lck, std::chrono::seconds(args->timeout_s_)) == std::cv_status::no_timeout) {
      continue;
    }

    // timeout
    args->stop_ = true;
    args->cb_();
    args->stop_ = false;

  }

}

RDMATimer::RDMATimer(size_t timeout_s, cb_func cb) 
  : timeout_s_(timeout_s), cb_(cb) {
  alive_.store(true);
  thread_ = new std::thread(RDMATimerFunc, this);
}

RDMATimer::~RDMATimer() {
  alive_ = false;
  start_.notify_all();

  thread_->join();
}


void RDMATimer::start() {
  start_.notify_one();
}

void RDMATimer::stop() {
  timer_.notify_one();
}