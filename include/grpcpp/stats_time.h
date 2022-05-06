#ifndef GRPCPP_STATS_TIME_H
#define GRPCPP_STATS_TIME_H
#include <grpc/support/sync.h>
#include <atomic>
#include <vector>
#include "absl/time/clock.h"
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
  GRPC_STATS_TIME_ADHOC_1,
  GRPC_STATS_TIME_ADHOC_2,
  GRPC_STATS_TIME_ADHOC_3,
  GRPC_STATS_TIME_ADHOC_4,
  GRPC_STATS_TIME_ADHOC_5,
  GRPC_STATS_TIME_MAX_OP_SIZE,
} grpc_stats_time;
typedef enum {
  GRPC_STATS_TIME_NANO,
  GRPC_STATS_TIME_MICRO,
  GRPC_STATS_TIME_MILLI
} grpc_stats_time_unit;

struct grpc_stats_time_entry {
  std::atomic_llong duration;
  std::atomic_llong count;
  std::atomic_llong max_time;
  bool scale;
  int group;
  grpc_stats_time_entry()
      : duration(0), count(0), max_time(0), scale(true), group(-1) {}

  inline void add(long long time) {
    duration += time;
    count++;
    bool ok = true;
    do {
      auto prev_max = max_time.load();
      if (time > prev_max) {
        ok = max_time.compare_exchange_weak(prev_max, time);
      }
    } while (!ok);
  }
};

typedef struct grpc_stats_time_data {
  std::vector<grpc_stats_time_entry*> stats_per_op;
  grpc_stats_time_unit unit;
} grpc_stats_time_data;

extern grpc_stats_time_data* grpc_stats_time_storage;

void grpc_stats_time_init(void);
void grpc_stats_time_shutdown(void);
void grpc_stats_time_enable();
void grpc_stats_time_disable();
void grpc_stats_time_print(void);
void grpc_stats_time_print(std::ostream& os);
std::string grpc_stats_time_op_to_str(int op);

void grpc_stats_time_add_custom(grpc_stats_time op, long long data);

void grpc_stats_time_add(grpc_stats_time op, const absl::Duration& time,
                         int group = -1);

class GRPCProfiler {
 public:
  explicit GRPCProfiler(grpc_stats_time op, int group = -1)
      : op_(op), group_(group) {
    begin_ = absl::Now();
  }

  ~GRPCProfiler() { grpc_stats_time_add(op_, (absl::Now() - begin_), group_); }

 private:
  grpc_stats_time op_;
  int group_;
  absl::Time begin_;
};

#endif  // GRPCPP_STATS_TIME_H
