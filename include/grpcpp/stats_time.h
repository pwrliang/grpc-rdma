#ifndef GRPCPP_STATS_TIME_H
#define GRPCPP_STATS_TIME_H
#include <grpc/support/sync.h>
#include <grpcpp/get_clock.h>
#include <atomic>
#include <mutex>
#include <vector>

typedef enum {
  GRPC_STATS_TIME_POLLABLE_EPOLL,
  GRPC_STATS_TIME_POLLSET_WORK,
  GRPC_STATS_TIME_TRANSPORT_DO_READ,
  GRPC_STATS_TIME_TRANSPORT_CONTINUE_READ,
  GRPC_STATS_TIME_TRANSPORT_READ_ALLOCATION_DONE,
  GRPC_STATS_TIME_TRANSPORT_HANDLE_READ,
  GRPC_STATS_TIME_TRANSPORT_READ,
  GRPC_STATS_TIME_TRANSPORT_FLUSH,
  GRPC_STATS_TIME_TRANSPORT_HANDLE_WRITE,
  GRPC_STATS_TIME_TRANSPORT_WRITE,
  GRPC_STATS_TIME_SEND_LAG,
  GRPC_STATS_TIME_RECV_LAG,
  GRPC_STATS_TIME_SEND,
  GRPC_STATS_TIME_SEND_MEMCPY,
  GRPC_STATS_TIME_SEND_IBV,
  GRPC_STATS_TIME_SEND_POST,
  GRPC_STATS_TIME_SEND_POLL,
  GRPC_STATS_TIME_SEND_SIZE,
  GRPC_STATS_TIME_CLIENT_PREPARE,
  GRPC_STATS_TIME_CLIENT_CQ_NEXT,
  GRPC_STATS_TIME_SERVER_PREPARE,
  GRPC_STATS_TIME_SERVER_CQ_NEXT,
  GRPC_STATS_TIME_RDMA_POLL,
  GRPC_STATS_TIME_ADHOC_1,
  GRPC_STATS_TIME_ADHOC_2,
  GRPC_STATS_TIME_ADHOC_3,
  GRPC_STATS_TIME_ADHOC_4,
  GRPC_STATS_TIME_ADHOC_5,
  GRPC_STATS_TIME_MAX_OP_SIZE,
} grpc_stats_time;

typedef enum {
  GRPC_STATS_TIME_MICRO,
  GRPC_STATS_TIME_MILLI,
  GRPC_STATS_TIME_SEC,
} grpc_stats_time_unit;

struct grpc_stats_time_entry {
  uint64_t duration;
  uint64_t count;
  cycles_t max_time;
  bool scale;
  int group;
  int thread_id;

  grpc_stats_time_entry()
      : duration(0), count(0), max_time(0), scale(true), group(-1) {}

  inline void add(cycles_t time) {
    duration += time;
    count++;
    max_time = std::max(max_time, time);
  }
};

typedef struct grpc_stats_time_data {
  std::vector<grpc_stats_time_entry*> stats_per_op;
  grpc_stats_time_unit unit;
  int thread_id;
} grpc_stats_time_data;

extern thread_local grpc_stats_time_data* grpc_stats_time_storage;
extern std::vector<grpc_stats_time_data*> grpc_stats_time_vec;
extern std::mutex grpc_stats_time_mtx;

void grpc_stats_time_init(int);
void grpc_stats_time_shutdown(void);
void grpc_stats_time_enable();
void grpc_stats_time_disable();
void grpc_stats_time_print(void);
void grpc_stats_time_print(std::ostream& os);
std::string grpc_stats_time_op_to_str(int op);

void grpc_stats_time_add_custom(grpc_stats_time op, long long data);

void grpc_stats_time_add(grpc_stats_time op, cycles_t cycles, int group = -1);

class GRPCProfiler {
 public:
  explicit GRPCProfiler(grpc_stats_time op, int group = -1);

  ~GRPCProfiler();

 private:
  grpc_stats_time op_;
  int group_;
  cycles_t begin_;
};

#endif  // GRPCPP_STATS_TIME_H
