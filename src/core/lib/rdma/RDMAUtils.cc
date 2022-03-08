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

int _rdma_internal_world_size_ = 0, _rdma_internal_world_rank_ = 0;

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

  if (_rdma_internal_world_rank_ == 0) {
    printf("device %s attribute: max_cqe = %ld, max_qp_wr = %d, max_sge = %d\n", 
          name, dev_attr.max_cqe, dev_attr.max_qp_wr, dev_attr.max_sge);
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


// -----< TimerPackage >-----
std::atomic_size_t TimerPackage::global_count(0);

TimerPackage::TimerPackage(size_t timeout_ms) : timeout_ms_(timeout_ms), local_id_(global_count.fetch_add(1)) {
  alive_.store(false);
  accumulative_timeout_ms_.store(0);
  thread_ = new std::thread(ThreadRunThis, this);
  // thread_ will wait in phase 1 after constructor
}

void TimerPackage::ThreadRunThis(TimerPackage* pkg) {
  std::unique_lock<std::mutex> start_lck(pkg->start_mu_);
  std::unique_lock<std::mutex> timer_lck(pkg->timer_mu_);
  pkg->accumulative_timeout_ms_.store(0);
  pkg->alive_.store(true);
  while (pkg->alive_.load()) {

    // phase 1:
    // wait for start_ 
    pkg->start_.wait(start_lck);
    // start_.notify_XXX and start_mu_.unlock are called

    // phase 2:
    // started, either be notified or timeout or not alive
    for (size_t i = 0; 
         pkg->alive_.load() && pkg->timer_.wait_for(timer_lck, std::chrono::milliseconds(pkg->timeout_ms_.load())) == std::cv_status::timeout;
         i++) {
      // timeout
      pkg->accumulative_timeout_ms_.fetch_add(pkg->timeout_ms_.load());
      // printf("Timer (%d): timeout (%lld ms x %d): %s\n", pkg->local_id_, pkg->timeout_ms_.load(), i + 1, pkg->message_);     
    }
    // not alive or be notified(never timeout or be notified after timeouts);
      
    // not alive: break from the while loop

    // notified: stopped
    // if never timeout: go to phase 1, stop caller return;
    // if timeouted: go to phase 1, stop caller blocked
  }
  return;
}

// expect timer now is in phase 1
TimerPackage::~TimerPackage() {
  while (!alive_.load()) { std::this_thread::yield(); }
  alive_.store(false);
  start_mu_.lock();
  start_.notify_all();
  start_mu_.unlock();
  timer_mu_.lock();
  timer_.notify_all();
  timer_mu_.unlock();
  thread_->join();
  // printf("Timer (%d) closed\n", local_id);
}

void TimerPackage::Start() {
  Start("");
}

void TimerPackage::Start(const char *format, ...) {
  message_ = nullptr;
  va_list args;
  va_start(args, format);
  if (vasprintf(&message_, format, args) == -1) {
      va_end(args);
      printf("return in start\n");
      return;
  }
  va_end(args);
  while (!alive_.load()) { std::this_thread::yield(); } // make sure stat_lck is created (start_mu_ is locked)
  start_thread_id_ = std::this_thread::get_id();
  start_mu_.lock(); // start_.wait is called
  start_.notify_one();
  start_mu_.unlock();
  // switch from phase 1 to phase 2
}

void TimerPackage::Stop() {
  if (std::this_thread::get_id() != start_thread_id_) printf("start and stop in different thread\n");
  while (!alive_.load()) { std::this_thread::yield(); }
  timer_mu_.lock();
  timer_.notify_one();
  timer_mu_.unlock();
  // notify timer in phase 2
  
  accumulative_timeout_ms_.store(0);
}