#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "absl/time/clock.h"

#include "include/grpc/impl/codegen/log.h"
#include "include/grpcpp/stats_time.h"

#include "src/core/lib/debug/VariadicTable.h"
#include "src/core/lib/debug/get_clock.h"
#include "src/core/lib/gpr/env.h"

std::atomic_bool grpc_stats_time_enabled;
thread_local int grpc_stats_time_slot = -1;
std::mutex grpc_stats_time_mu;
std::vector<std::unique_ptr<grpc_stats_time_data>> grpc_stats_time_storage;

GRPCProfiler::GRPCProfiler(grpc_stats_time op) : op_(op), begin_(absl::Now()) {}

GRPCProfiler::~GRPCProfiler() {
  grpc_stats_time_add(op_, absl::ToInt64Nanoseconds(absl::Now() - begin_));
}

grpc_stats_time_unit grpc_stats_time_get_unit() {
  grpc_stats_time_unit u = GRPC_STATS_TIME_MICRO;
  auto* unit = gpr_getenv("GRPC_PROFILING_UNIT");

  if (unit != nullptr) {
    auto s_unit = std::string(unit);

    if (s_unit == "micro") {
      u = GRPC_STATS_TIME_MICRO;
    } else if (s_unit == "milli") {
      u = GRPC_STATS_TIME_MILLI;
    } else if (s_unit == "s") {
      u = GRPC_STATS_TIME_SEC;
    } else {
      printf("Invalid profiling time unit: %s\n", unit);
      exit(1);
    }
  }

  return u;
}

void grpc_stats_time_init(int slot) {
  grpc_stats_time_slot = slot;
  auto storage = std::make_unique<grpc_stats_time_data>();

  storage->slot = slot;

  for (int i = 0; i < GRPC_STATS_TIME_MAX_OP_SIZE; i++) {
    storage->stats_per_op[i].clear();
  }
  std::lock_guard<std::mutex> lg(grpc_stats_time_mu);
  grpc_stats_time_storage.push_back(std::move(storage));
}

void grpc_stats_time_shutdown() {
  std::lock_guard<std::mutex> lg(grpc_stats_time_mu);
  grpc_stats_time_disable();
  grpc_stats_time_storage.clear();
}

void grpc_stats_time_print();

void grpc_stats_time_enable() { grpc_stats_time_enabled = true; }

void grpc_stats_time_disable() { grpc_stats_time_enabled = false; }

void grpc_stats_time_add(grpc_stats_time op, int64_t val) {
  if (grpc_stats_time_enabled && grpc_stats_time_slot != -1) {
    auto* storage = grpc_stats_time_storage[grpc_stats_time_slot].get();

    GPR_ASSERT(val < GRPC_PROFILING_MAX_VALUE);
    storage->stats_per_op[op].add(val);
    storage->stats_per_op[op].scale = true;
  }
}

void grpc_stats_time_add_custom(grpc_stats_time op, int64_t val) {
  if (grpc_stats_time_enabled && grpc_stats_time_slot != -1) {
    auto* storage = grpc_stats_time_storage[grpc_stats_time_slot].get();

    GPR_ASSERT(val < GRPC_PROFILING_MAX_VALUE);
    storage->stats_per_op[op].add(val);
    storage->stats_per_op[op].scale = false;
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
    case GRPC_STATS_TIME_PAIR_SEND:
      return "PAIR_SEND";
    case GRPC_STATS_TIME_PAIR_RECV:
      return "PAIR_RECV";
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
    case GRPC_STATS_TIME_BEGIN_WORKER:
      return "BEGIN_WORKER";
    case GRPC_STATS_TIME_ASYNC_NEXT_INTERNAL:
      return "ASYNC_NEXT_INTERNAL";
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
  double mhz = grpc_stats_time_get_cpu_mhz();

  int scale;
  std::string unit;

  switch (grpc_stats_time_get_unit()) {
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

  os << "=================================Profiling "
        "Result================================="
     << std::endl;
  os << "Unit " << unit << std::endl;
  os << "Frequency " << mhz << " mhz" << std::endl;

  for (auto& storage : grpc_stats_time_storage) {
    bool has_data = false;
    VariadicTable<std::string, size_t, double, double, double, double, double>
        vt({"Name", "Count", "Mean", "P50", "P95", "P99", "MAX"});
    vt.setColumnPrecision({0, 0, 2, 2, 2, 2, 2});
    vt.setColumnFormat(
        {VariadicTableColumnFormat::AUTO, VariadicTableColumnFormat::AUTO,
         VariadicTableColumnFormat::FIXED, VariadicTableColumnFormat::FIXED,
         VariadicTableColumnFormat::FIXED, VariadicTableColumnFormat::FIXED,
         VariadicTableColumnFormat::FIXED});

    for (int op = 0; op < GRPC_STATS_TIME_MAX_OP_SIZE; op++) {
      auto& time_entry = storage->stats_per_op[op];
      std::string suffix = "";
      auto count = time_entry.count;

      double mean_val = hdr_mean(time_entry.histogram_);
      double p50 = hdr_value_at_percentile(time_entry.histogram_, 0.5);
      double p95 = hdr_value_at_percentile(time_entry.histogram_, 0.95);
      double p99 = hdr_value_at_percentile(time_entry.histogram_, 0.99);
      double max_val = hdr_max(time_entry.histogram_);

      if (count > 0) {
        if (time_entry.scale) {
          //          mean_val = mean_val / mhz / scale;
          //          p50 = p50 / mhz / scale;
          //          p95 = p95 / mhz / scale;
          //          p99 = p99 / mhz / scale;
          //          max_val = max_val / mhz / scale;
          mean_val = mean_val / 1000 / scale;
          p50 = p50 / 1000 / scale;
          p95 = p95 / 1000 / scale;
          p99 = p99 / 1000 / scale;
          max_val = max_val / scale;
        } else {
          suffix = " (custom)";
        }

        vt.addRow(grpc_stats_time_op_to_str(op) + suffix, time_entry.count,
                  mean_val, p50, p95, p99, max_val);
        has_data = true;
      }
    }

    if (has_data) {
      os << "Slot: " << storage->slot << std::endl;
      vt.print(os);
    }
  }

  os << "======================================================================"
        "============"
     << std::endl;
}

void grpc_stats_time_print() { grpc_stats_time_print(std::cout); }

double grpc_stats_time_get_cpu_mhz() {
  static double mhz = get_cpu_mhz(0);
  return mhz;
}
