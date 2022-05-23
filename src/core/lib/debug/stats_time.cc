#include "include/grpcpp/stats_time.h"
#include <grpc/support/sync.h>
#include <mutex>
#include "absl/time/clock.h"
#include "include/grpc/impl/codegen/log.h"
#include "src/core/lib/debug/VariadicTable.h"
thread_local grpc_stats_time_data* grpc_stats_time_storage = nullptr;
bool grpc_stats_time_enabled = false;
std::vector<grpc_stats_time_data*> grpc_stats_time_vec;
std::mutex grpc_stats_time_mtx;

void grpc_stats_time_init(int thread_id) {
  auto* unit = getenv("GRPC_PROFILING");

  if (unit != nullptr) {
    std::lock_guard<std::mutex> lg(grpc_stats_time_mtx);

    if (grpc_stats_time_storage != nullptr) {
      gpr_log(GPR_ERROR, "grpc_stats_time_storage already initialized");
      return;
    }

    auto s_unit = std::string(unit);

    grpc_stats_time_storage = new grpc_stats_time_data();
    grpc_stats_time_storage->thread_id = thread_id;

    if (s_unit == "nano") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_NANO;
    } else if (s_unit == "micro") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_MICRO;
    } else if (s_unit == "milli") {
      grpc_stats_time_storage->unit = GRPC_STATS_TIME_MILLI;
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

void grpc_stats_time_add(grpc_stats_time op, const absl::Duration& time,
                         int group) {
  if (grpc_stats_time_storage != nullptr && grpc_stats_time_enabled) {
    grpc_stats_time_storage->stats_per_op[op]->add(ToInt64Nanoseconds(time));
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
    case GRPC_STATS_TIME_SEND_LAG:
      return "SEND_LAG";
    case GRPC_STATS_TIME_RECV_LAG:
      return "RECV_LAG";
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
    case GRPC_STATS_TIME_CLIENT_PREPARE:
      return "CLIENT_PREPARE";
    case GRPC_STATS_TIME_CLIENT_CQ_NEXT:
      return "CLIENT_CQ_NEXT";
    case GRPC_STATS_TIME_SERVER_PREPARE:
      return "SERVER_PREPARE";
    case GRPC_STATS_TIME_SERVER_CQ_NEXT:
      return "SERVER_CQ_NEXT";
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
    default:
      return std::to_string(static_cast<int>(op));
  }
}

double median(std::vector<int64_t>& v) {
  size_t n = v.size() / 2;
  std::nth_element(v.begin(), v.begin() + n, v.end());
  int vn = v[n];
  if (v.size() % 2 == 1) {
    return vn;
  } else {
    std::nth_element(v.begin(), v.begin() + n - 1, v.end());
    return 0.5 * (vn + v[n - 1]);
  }
}

void grpc_stats_time_print(std::ostream& os) {
  std::lock_guard<std::mutex> lg(grpc_stats_time_mtx);

  for (auto time_storage : grpc_stats_time_vec) {
    int scale;
    std::string unit;
    switch (time_storage->unit) {
      case GRPC_STATS_TIME_NANO: {
        unit = "nano";
        scale = 1;
        break;
      }
      case GRPC_STATS_TIME_MICRO: {
        unit = "micro";
        scale = 1000;
        break;
      }
      case GRPC_STATS_TIME_MILLI: {
        unit = "milli";
        scale = 1000 * 1000;
        break;
      }
    }

    VariadicTable<std::string, size_t, int64_t, int64_t, int64_t> vt(
        {"Name", "Count", "Time (" + unit + ")", "Time Avg", "Time Max"});
    int max_group_id = -1;
    for (int op = 0; op < GRPC_STATS_TIME_MAX_OP_SIZE; op++) {
      max_group_id = std::max(max_group_id,
                              time_storage->stats_per_op[op]->group);
    }
    std::vector<int64_t> total_count, total_time, max_time;
    total_count.resize(max_group_id + 1, 0);
    total_time.resize(max_group_id + 1, 0);
    max_time.resize(max_group_id + 1, 0);

    for (int op = 0; op < GRPC_STATS_TIME_MAX_OP_SIZE; op++) {
      auto& time_entry = *time_storage->stats_per_op[op];
      auto curr_scale = scale;
      std::string suffix = "";
      if (!time_entry.scale) {
        curr_scale = 1;
        suffix = " (not time)";
      }
      int64_t curr_max_time = time_entry.max_time / curr_scale;
      auto count = time_entry.count.load();
      auto time = time_entry.duration.load() / curr_scale;

      if (count > 0) {
        int group = time_entry.group;
        if (group != -1) {
          suffix += " (group " + std::to_string(time_entry.group) + ")";
          total_count[group] += time_entry.count;
          total_time[group] += time;
          max_time[group] = std::max(max_time[group], curr_max_time);
        }

        vt.addRow(grpc_stats_time_op_to_str(op) + suffix, time_entry.count,
                  time, time / count, curr_max_time);
      }
    }

    for (int group = 0; group <= max_group_id; group++) {
      if (total_count[group] > 0) {
        vt.addRow("Group " + std::to_string(group), total_count[group],
                  total_time[group], total_time[group] / total_count[group],
                  max_time[group]);
      }
    }

    vt.print(os);
  }
}

void grpc_stats_time_print() { grpc_stats_time_print(std::cout); }