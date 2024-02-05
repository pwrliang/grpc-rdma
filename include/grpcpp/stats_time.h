#ifndef GRPCPP_STATS_TIME_H
#define GRPCPP_STATS_TIME_H
#include <array>
#include <ostream>
#include "hdr/hdr_histogram.h"
#define GRPC_PROFILING_MAX_VALUE (60 * 1000 * 1000 * 1000L)

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
  GRPC_STATS_TIME_SEND,
  GRPC_STATS_TIME_SEND_MEMCPY,
  GRPC_STATS_TIME_SEND_IBV,
  GRPC_STATS_TIME_SEND_POST,
  GRPC_STATS_TIME_SEND_POLL,
  GRPC_STATS_TIME_SEND_SIZE,
  GRPC_STATS_TIME_RECV_SIZE,
  GRPC_STATS_TIME_CLIENT_PREPARE,
  GRPC_STATS_TIME_CLIENT_CQ_NEXT,
  GRPC_STATS_TIME_SERVER_RPC_REQUEST,
  GRPC_STATS_TIME_SERVER_RPC_FINISH,
  GRPC_STATS_TIME_SERVER_CQ_NEXT,
  GRPC_STATS_TIME_RDMA_POLL,
  GRPC_STATS_TIME_BEGIN_WORKER,
  GRPC_STATS_TIME_ASYNC_NEXT_INTERNAL,
  GRPC_STATS_TIME_SEND_COPY_BW,
  GRPC_STATS_TIME_RECV_COPY_BW,
  GRPC_STATS_TIME_FINALIZE_RESULT,
  GRPC_STATS_TIME_DESERIALIZE,
  GRPC_STATS_TIME_ADHOC_1,
  GRPC_STATS_TIME_ADHOC_2,
  GRPC_STATS_TIME_ADHOC_3,
  GRPC_STATS_TIME_ADHOC_4,
  GRPC_STATS_TIME_ADHOC_5,
  GRPC_STATS_TIME_ADHOC_6,
  GRPC_STATS_TIME_ADHOC_7,
  GRPC_STATS_TIME_ADHOC_8,
  GRPC_STATS_TIME_ADHOC_9,
  GRPC_STATS_TIME_ADHOC_10,
  GRPC_STATS_TIME_MAX_OP_SIZE,
} grpc_stats_time;

typedef enum {
  GRPC_STATS_TIME_MICRO,
  GRPC_STATS_TIME_MILLI,
  GRPC_STATS_TIME_SEC,
} grpc_stats_time_unit;

struct grpc_stats_time_entry {
  uint64_t count;
  struct hdr_histogram* histogram_;
  bool scale;

  grpc_stats_time_entry() : count(0), scale(true) {
    hdr_init(1,                         // Minimum value
             GRPC_PROFILING_MAX_VALUE,  // Maximum value
             3,                         // Number of significant figures
             &histogram_);              // Pointer to initialise
  }

  grpc_stats_time_entry(const grpc_stats_time_entry& other) {
    hdr_init(1,                         // Minimum value
             GRPC_PROFILING_MAX_VALUE,  // Maximum value
             3,                         // Number of significant figures
             &histogram_);              // Pointer to initialise
    hdr_add(histogram_, other.histogram_);
  }

  grpc_stats_time_entry& operator=(const grpc_stats_time_entry& other) {
    if (&other == this) {
      return *this;
    }
    hdr_reset(histogram_);
    hdr_add(histogram_, other.histogram_);
    return *this;
  }

  ~grpc_stats_time_entry() { hdr_close(histogram_); }

  inline void add(int64_t val) {
    hdr_record_value(histogram_, val);
    count++;
  }

  inline void clear() {
    hdr_reset(histogram_);
    count = 0;
  }
};

typedef struct grpc_stats_time_data {
  std::array<grpc_stats_time_entry, GRPC_STATS_TIME_MAX_OP_SIZE> stats_per_op;
  grpc_stats_time_unit unit;
  int slot;
} grpc_stats_time_data;

void grpc_stats_time_init(int);
void grpc_stats_time_enable();
void grpc_stats_time_disable();
void grpc_stats_time_print(void);
void grpc_stats_time_print(std::ostream& os);
void grpc_stats_time_add(grpc_stats_time op, int64_t val);
void grpc_stats_time_add_custom(grpc_stats_time op, int64_t val);
void grpc_stats_time_shutdown();

double grpc_stats_time_get_cpu_mhz();

class GRPCProfiler {
 public:
  explicit GRPCProfiler(grpc_stats_time op);

  ~GRPCProfiler();

 private:
  grpc_stats_time op_;
  uint64_t begin_;
};

#endif  // GRPCPP_STATS_TIME_H
