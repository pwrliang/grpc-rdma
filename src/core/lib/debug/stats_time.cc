#include "include/grpcpp/stats_time.h"
#include <grpc/support/sync.h>
#include <mutex>
#include "include/grpc/impl/codegen/log.h"
#include "include/grpcpp/get_clock.h"
#include "src/core/lib/debug/VariadicTable.h"
thread_local grpc_stats_time_data* grpc_stats_time_storage = nullptr;
bool grpc_stats_time_enabled = false;
std::vector<grpc_stats_time_data*> grpc_stats_time_vec;
std::mutex grpc_stats_time_mtx;
std::atomic_int32_t profiler_thread_id(100);

GRPCProfiler::GRPCProfiler(grpc_stats_time op, int group)
    : op_(op), group_(group), begin_(get_cycles()) {}

GRPCProfiler::~GRPCProfiler() {
  grpc_stats_time_add(op_, (get_cycles() - begin_), group_);
}

void grpc_stats_time_init(int thread_id) {
  auto* unit = getenv("GRPC_PROFILING");

  if (unit != nullptr) {
    if (grpc_stats_time_storage != nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lg(grpc_stats_time_mtx);
    auto s_unit = std::string(unit);

    GPR_ASSERT(thread_id < 100);
    grpc_stats_time_storage = new grpc_stats_time_data();
    grpc_stats_time_storage->thread_id = thread_id;

    if (s_unit == "micro") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_MICRO;
    } else if (s_unit == "milli") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_MILLI;
    } else if (s_unit == "s") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_SEC;
    } else {
      printf("Invalid profiling time unit: %s\n", unit);
      exit(1);
    }

    for (int i = 0; i < GRPC_STATS_TIME_MAX_OP_SIZE; i++) {
      grpc_stats_time_storage->stats_per_op.push_back(
          new grpc_stats_time_entry());
    }
    grpc_stats_time_vec.push_back(grpc_stats_time_storage);
  }
}

void grpc_stats_time_init() {
  auto* unit = getenv("GRPC_PROFILING");

  if (unit != nullptr) {
    if (grpc_stats_time_storage != nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lg(grpc_stats_time_mtx);
    auto s_unit = std::string(unit);

    grpc_stats_time_storage = new grpc_stats_time_data();
    grpc_stats_time_storage->thread_id = profiler_thread_id++;

    if (s_unit == "micro") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_MICRO;
    } else if (s_unit == "milli") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_MILLI;
    } else if (s_unit == "s") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_SEC;
    } else {
      printf("Invalid profiling time unit: %s\n", unit);
      exit(1);
    }

    for (int i = 0; i < GRPC_STATS_TIME_MAX_OP_SIZE; i++) {
      grpc_stats_time_storage->stats_per_op.push_back(
          new grpc_stats_time_entry());
    }
    grpc_stats_time_vec.push_back(grpc_stats_time_storage);
  }
}

void grpc_stats_time_print();

void grpc_stats_time_shutdown() {
  if (grpc_stats_time_storage != nullptr) {
    for (int i = 0; i < GRPC_STATS_TIME_MAX_OP_SIZE; i++) {
      delete grpc_stats_time_storage->stats_per_op[i];
    }
    delete grpc_stats_time_storage;
    grpc_stats_time_storage = nullptr;
  }
}

void grpc_stats_time_enable() { grpc_stats_time_enabled = true; }

void grpc_stats_time_disable() { grpc_stats_time_enabled = false; }

void grpc_stats_time_add_custom(grpc_stats_time op, long long time) {
  if (grpc_stats_time_storage != nullptr && grpc_stats_time_enabled) {
    grpc_stats_time_storage->stats_per_op[op]->add(time);
    grpc_stats_time_storage->stats_per_op[op]->scale = false;
  }
}

void grpc_stats_time_add(grpc_stats_time op, cycles_t cycles, int group) {
  if (grpc_stats_time_storage != nullptr && grpc_stats_time_enabled) {
    grpc_stats_time_storage->stats_per_op[op]->add(cycles);
    grpc_stats_time_storage->stats_per_op[op]->group = group;
  }
}

std::string grpc_stats_time_op_to_str(int op) {
  switch (op) {
    case GRPC_STATS_TIME_POLLABLE_EPOLL:
      return "POLLABLE_EPOLL";
    case GRPC_STATS_TIME_POLLSET_WORK:
      return "POLLSET_WORK";
    case GRPC_STATS_TIME_TRANSPORT_DO_READ:
      return "TRANSPORT_DO_READ";
    case GRPC_STATS_TIME_TRANSPORT_CONTINUE_READ:
      return "TRANSPORT_CONTINUE_READ";
    case GRPC_STATS_TIME_TRANSPORT_READ_ALLOCATION_DONE:
      return "TRANSPORT_READ_ALLOCATION_DONE";
    case GRPC_STATS_TIME_TRANSPORT_HANDLE_READ:
      return "TRANSPORT_HANDLE_READ";
    case GRPC_STATS_TIME_TRANSPORT_READ:
      return "TRANSPORT_READ";
    case GRPC_STATS_TIME_TRANSPORT_FLUSH:
      return "TRANSPORT_FLUSH";
    case GRPC_STATS_TIME_TRANSPORT_HANDLE_WRITE:
      return "TRANSPORT_HANDLE_WRITE";
    case GRPC_STATS_TIME_TRANSPORT_WRITE:
      return "TRANSPORT_WRITE";
    case GRPC_STATS_TIME_SEND:
      return "SEND";
    case GRPC_STATS_TIME_SEND_MEMCPY:
      return "SEND_MEMCPY";
    case GRPC_STATS_TIME_SEND_IBV:
      return "SEND_IBV";
    case GRPC_STATS_TIME_SEND_POST:
      return "SEND_POST";
    case GRPC_STATS_TIME_SEND_POLL:
      return "SEND_POLL";
    case GRPC_STATS_TIME_SEND_SIZE:
      return "SEND_SIZE";
    case GRPC_STATS_TIME_RECV_SIZE:
      return "RECV_SIZE";
    case GRPC_STATS_TIME_CLIENT_PREPARE:
      return "CLIENT_PREPARE";
    case GRPC_STATS_TIME_CLIENT_CQ_NEXT:
      return "CLIENT_CQ_NEXT";
    case GRPC_STATS_TIME_SERVER_RPC_REQUEST:
      return "SERVER_RPC_REQUEST";
    case GRPC_STATS_TIME_SERVER_RPC_FINISH:
      return "SERVER_RPC_FINISH";
    case GRPC_STATS_TIME_SERVER_CQ_NEXT:
      return "SERVER_CQ_NEXT";
    case GRPC_STATS_TIME_RDMA_POLL:
      return "RDMA_POLL";
    case GRPC_STATS_TIME_BEGIN_WORKER:
      return "BEGIN_WORKER";
    case GRPC_STATS_TIME_ASYNC_NEXT_INTERNAL:
      return "ASYNC_NEXT_INTERNAL";
    case GRPC_STATS_TIME_SEND_COPY_BW:
      return "SEND_COPY_BW";
    case GRPC_STATS_TIME_RECV_COPY_BW:
      return "RECV_COPY_BW";
    case GRPC_STATS_TIME_FINALIZE_RESULT:
      return "FINALIZE_RESULT";
    case GRPC_STATS_TIME_DESERIALIZE:
      return "DESERIALIZE";
    case GRPC_STATS_TIME_ADHOC_1:
      return "ADHOC_1";
    case GRPC_STATS_TIME_ADHOC_2:
      return "ADHOC_2";
    case GRPC_STATS_TIME_ADHOC_3:
      return "ADHOC_3";
    case GRPC_STATS_TIME_ADHOC_4:
      return "ADHOC_4";
    case GRPC_STATS_TIME_ADHOC_5:
      return "ADHOC_5";
    case GRPC_STATS_TIME_ADHOC_6:
      return "ADHOC_6";
    case GRPC_STATS_TIME_ADHOC_7:
      return "ADHOC_7";
    case GRPC_STATS_TIME_ADHOC_8:
      return "ADHOC_8";
    case GRPC_STATS_TIME_ADHOC_9:
      return "ADHOC_9";
    case GRPC_STATS_TIME_ADHOC_10:
      return "ADHOC_10";
    default:
      return std::to_string(static_cast<int>(op));
  }
}

void grpc_stats_time_print(std::ostream& os) {
  int no_cpu_freq_fail = 0;
  double mhz = get_cpu_mhz(no_cpu_freq_fail);
  std::lock_guard<std::mutex> lg(grpc_stats_time_mtx);

  for (auto time_storage : grpc_stats_time_vec) {
    int scale;
    std::string unit;
    bool has_data = false;

    switch (time_storage->unit) {
      case GRPC_STATS_TIME_MICRO: {
        unit = "us";
        scale = 1;
        break;
      }
      case GRPC_STATS_TIME_MILLI: {
        unit = "ms";
        scale = 1000;
        break;
      }
      case GRPC_STATS_TIME_SEC: {
        unit = "s";
        scale = 1000 * 1000;
        break;
      }
    }

    VariadicTable<std::string, size_t, int64_t, double, double> vt(
        {"Name", "Count", "Time (" + unit + ")", "Time Avg (us)",
         "Time Max (" + unit + ")"});
    vt.setColumnPrecision({0, 0, 0, 2, 2});
    vt.setColumnFormat(
        {VariadicTableColumnFormat::AUTO, VariadicTableColumnFormat::AUTO,
         VariadicTableColumnFormat::FIXED, VariadicTableColumnFormat::FIXED,
         VariadicTableColumnFormat::FIXED});
    int max_group_id = -1;
    for (int op = 0; op < GRPC_STATS_TIME_MAX_OP_SIZE; op++) {
      max_group_id =
          std::max(max_group_id, time_storage->stats_per_op[op]->group);
    }
    std::vector<int64_t> total_count;
    std::vector<double> total_time, max_time;
    total_count.resize(max_group_id + 1, 0);
    total_time.resize(max_group_id + 1, 0);
    max_time.resize(max_group_id + 1, 0);

    for (int op = 0; op < GRPC_STATS_TIME_MAX_OP_SIZE; op++) {
      auto& time_entry = *time_storage->stats_per_op[op];
      std::string suffix = "";

      auto count = time_entry.count;
      int group = time_entry.group;

      if (count > 0) {
        double curr_max_time;
        double time, time_in_micro;

        if (time_entry.scale) {
          curr_max_time = time_entry.max_time / mhz / scale;
          time_in_micro = time_entry.duration / mhz;
          time = time_in_micro / scale;
        } else {
          suffix = " (not time)";
          curr_max_time = time_entry.max_time;
          time_in_micro = time = time_entry.duration;
        }

        if (group != -1) {
          suffix += " (group " + std::to_string(time_entry.group) + ")";
          total_count[group] += time_entry.count;
          total_time[group] += time;
          max_time[group] = std::max(max_time[group], curr_max_time);
        }

        vt.addRow(grpc_stats_time_op_to_str(op) + suffix, time_entry.count,
                  time, time_in_micro / count, curr_max_time);
        has_data = true;
      }
    }

    for (int group = 0; group <= max_group_id; group++) {
      if (total_count[group] > 0) {
        vt.addRow("Group " + std::to_string(group), total_count[group],
                  total_time[group], total_time[group] / total_count[group],
                  max_time[group]);
      }
    }
    if (has_data) {
      os << "TID: " << time_storage->thread_id << std::endl;
      vt.print(os);
    }
  }
}

void grpc_stats_time_print() { grpc_stats_time_print(std::cout); }

double grpc_stats_time_get_cpu_mhz() {
  static double mhz = get_cpu_mhz(0);
  return mhz;
}
