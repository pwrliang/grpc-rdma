#ifndef GRPC_CORE_LIB_RDMA_RDMA_UTILS_H
#define GRPC_CORE_LIB_RDMA_RDMA_UTILS_H

#include <grpc/support/log.h>
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

#define MEM_BAR()                \
  asm volatile("" ::: "memory"); \
  asm volatile("mfence" ::: "memory");  // Compiler and Hardware barrier

enum class RDMAPollerMode { kServer, kClient, kBoth, kNone };

class RDMAConfig {
  RDMAConfig() { init(); }

 public:
  static RDMAConfig& GetInstance() {
    static RDMAConfig inst;
    return inst;
  }

  RDMAPollerMode get_poller_mode() const { return poller_mode_; }

  int get_polling_timeout() const { return polling_timeout_; }

  int get_polling_thread_num() const { return polling_thread_num_; }

  bool is_affinity() const { return affinity_; }

  bool is_polling_yield() const { return polling_yield_; }

  size_t get_ring_buffer_size() const { return ring_buffer_size_; }

  size_t get_send_chunk_size() const { return send_chunk_size_; }

  bool is_zero_copy() const { return zero_copy_; }

 private:
  void init() {
    // BPEV dedicated
    char* s_val = getenv("GRPC_RDMA_BPEV_POLLER");

    if (s_val == nullptr || strcmp(s_val, "server") == 0) {
      poller_mode_ = RDMAPollerMode::kServer;
    } else if (strcmp(s_val, "client") == 0) {
      poller_mode_ = RDMAPollerMode::kClient;
    } else if (strcmp(s_val, "both") == 0) {
      poller_mode_ = RDMAPollerMode::kBoth;
    } else if (strcmp(s_val, "none") == 0) {
      poller_mode_ = RDMAPollerMode::kNone;
    } else {
      gpr_log(GPR_ERROR, "Invalid GRPC_RDMA_POLLER: %s", s_val);
      abort();
    }

    s_val = getenv("GRPC_RDMA_BPEV_POLLING_THREAD");

    if (s_val != nullptr) {
      int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

      polling_thread_num_ = atoi(s_val);
      if (polling_thread_num_ <= 0 || polling_thread_num_ > num_cores) {
        gpr_log(GPR_ERROR, "Invalid env GRPC_RDMA_MAX_POLLER: %d",
                polling_thread_num_);
        abort();
      }
    } else {
      polling_thread_num_ = 1;
    }

    s_val = getenv("GRPC_RDMA_BPEV_POLLING_TIMEOUT");
    if (s_val != nullptr) {
      polling_timeout_ = atoi(s_val);
    } else {
      polling_timeout_ = 0;
    }

    s_val = getenv("GRPC_RDMA_AFFINITY");
    affinity_ = s_val == nullptr || strcmp(s_val, "true") == 0;

    // BP, BPEV
    s_val = getenv("GRPC_RDMA_POLLING_YIELD");
    polling_yield_ = s_val == nullptr || strcmp(s_val, "true") == 0;

    // For all
    s_val = getenv("GRPC_RDMA_RING_BUFFER_SIZE");
    if (s_val != nullptr) {
      ring_buffer_size_ = atoll(s_val);
    } else {
      ring_buffer_size_ = 1024ull * 1024 * 10;
    }

    s_val = getenv("GRPC_RDMA_SEND_CHUNK_SIZE");
    if (s_val != nullptr) {
      send_chunk_size_ = atoll(s_val);
    } else {
      send_chunk_size_ = 4 * 1024 * 1024;
    }

    s_val = getenv("GRPC_RDMA_ZEROCOPY_ENABLE");
    // zero_copy_ = s_val == nullptr || strcmp(s_val, "true") == 0;
    zero_copy_ = false;
  }

  RDMAPollerMode poller_mode_;
  int polling_timeout_;
  int polling_thread_num_;
  bool polling_yield_;
  bool affinity_;
  size_t ring_buffer_size_;
  size_t send_chunk_size_;
  bool zero_copy_;
};

class MemRegion {
 public:
  const static int rw_flag =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
  const static int w_flag = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

  MemRegion() : remote(true) {}
  virtual ~MemRegion() { dereg(); }

  // register local_mr from ibv_reg_mr, 0 means successful, -1 means failure.
  int RegisterLocal(std::shared_ptr<ibv_pd> pd, void* mem, size_t size,
                    int flag = rw_flag);

  // set remote_mr, return 0.
  int RegisterRemote(void* mem, uint32_t rkey, size_t len);

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
  // if it is local_mr, deregister its mr, set remote as true
  void dereg();

  std::shared_ptr<ibv_mr> local_mr;  // local memory region
  ibv_mr remote_mr;                  // remote memory region
  bool remote;
};

class RDMANode {
  RDMANode() { open(IBV_DEV_NAME); }
 public:
  const static int ib_port = 1;
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
  std::thread async_ev_thread_;
  bool monitor_ev_;

  void open(const char* name);
  void close();
};

void init_sge(ibv_sge* sge, void* lc_addr, size_t sz, uint32_t lkey);

void init_sr(ibv_send_wr* sr, ibv_sge* sge, ibv_wr_opcode opcode, void* rt_addr,
             uint32_t rkey, int num_sge, uint32_t imm_data, size_t id,
             ibv_send_wr* next);

void init_rr(ibv_recv_wr* rr, ibv_sge* sge, int num_sge);

int modify_qp_to_init(ibv_qp* qp);

int modify_qp_to_rtr(struct ibv_qp* qp, uint32_t remote_qpn,
                     uint32_t remote_psn, uint16_t dlid, union ibv_gid dgid,
                     uint8_t link_layer);

int modify_qp_to_rts(struct ibv_qp* qp, uint32_t sq_psn);

int sync_data(int fd, const char* local, char* remote, const size_t sz);

void barrier(int fd);

void print_async_event(struct ibv_context *ctx,
                       struct ibv_async_event *event);

#endif  // GRPC_CORE_LIB_RDMA_RDMA_UTILS_H