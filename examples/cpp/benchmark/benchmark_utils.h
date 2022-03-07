#ifndef BENCHMARK_UTILS
#define BENCHMARK_UTILS

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <thread>
#include <sys/time.h>
#include <unistd.h>
#include <mpi.h>
#include <getopt.h>
#include <sstream>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "sys/times.h"
#include "sys/vtimes.h"
#include "../../../src/core/lib/rdma/RDMAUtils.h"

extern int _rdma_internal_world_size_, _rdma_internal_world_rank_;

int mathToInt(std::string math) {
    std::stringstream mathStrm(math);
    int result;
    mathStrm >> result;
    char op;
    int num;
    while(mathStrm >> op >> num) result *= num;
    return result;
}

std::vector<int> Split2Int(const std::string& str, char delim) {
  std::vector<int> ints;
  size_t start;
  size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    ints.push_back(mathToInt(str.substr(start, end - start)));
  }
  return ints;
}

int random(int min, int max) {
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}

typedef struct GlobalCPUTimePackage {
  int processors_num = 0;
  clock_t cpu;
  clock_t sys_cpu;
  clock_t user_cpu;
} GlobalCPUTimePackage;

void global_cpu_measure_init(GlobalCPUTimePackage& pkg) {
  struct tms time_sample;
  char line[128];

  pkg.cpu = times(&time_sample);
  pkg.sys_cpu = time_sample.tms_stime;
  pkg.user_cpu = time_sample.tms_utime;

  FILE* file = fopen("/proc/cpuinfo", "r");
  while (fgets(line, 128, file) != nullptr) {
    if (strncmp(line, "processor", 9) == 0) pkg.processors_num++;
  }
  fclose(file);
}

double global_cpu_measure(GlobalCPUTimePackage& pkg) {
  struct tms time_sample;
  double percent = 0.0;

  clock_t current_cpu = times(&time_sample);
  if (current_cpu <= pkg.cpu || time_sample.tms_stime < pkg.sys_cpu || time_sample.tms_utime < pkg.user_cpu) {
    percent = -1.0;
  } else {
    percent = (time_sample.tms_stime - pkg.sys_cpu) + (time_sample.tms_utime - pkg.user_cpu);
    percent /= (current_cpu - pkg.cpu);
    percent /= pkg.processors_num;
    percent *= 100;
  }

  pkg.cpu = current_cpu;
  pkg.sys_cpu = time_sample.tms_stime;
  pkg.user_cpu = time_sample.tms_utime;

  return percent;
}

typedef struct ProcessCPUTimePackage {
  unsigned long long total_user;
  unsigned long long total_user_low;
  unsigned long long total_sys;
  unsigned long long total_idle;
} ProcessCPUTimePackage;

void process_cpu_measure_init(ProcessCPUTimePackage& pkg) {
  FILE* file = fopen("/proc/stat", "r");
  fscanf(file, "cpu %llu %llu %llu %llu", &pkg.total_user, &pkg.total_user_low,
      &pkg.total_sys, &pkg.total_idle);
  fclose(file);
}

double process_cpu_measure(ProcessCPUTimePackage& pkg) {
  unsigned long long total_user, total_user_low, total_sys, total_idle;
  double percent = 0.0;

  FILE* file = fopen("/proc/stat", "r");
  fscanf(file, "cpu %llu %llu %llu %llu", &total_user, &total_user_low,
      &total_sys, &total_idle);
  fclose(file);

  if (total_user < pkg.total_user || total_user_low < pkg.total_user_low || total_sys < pkg.total_sys || total_idle < pkg.total_idle) {
    percent = -1.0;
  } else {
    percent = (total_user - pkg.total_user) + (total_user_low - pkg.total_user_low) + (total_sys - pkg.total_sys);
    percent /= percent + (total_idle - pkg.total_idle);
    percent *= 100;
  }

  pkg.total_user = total_user;
  pkg.total_user_low = total_user_low;
  pkg.total_sys = total_sys;
  pkg.total_idle = total_idle;

  return percent;
}

// class CPUMeasurePackage {
//   public:
//     CPUMeasurePackage(size_t time_interval_ms);

//   private:
//     std::thread* thread_;

//     GlobalCPUTimePackage global_pkg;
//     std::vector<double> global_cpu_list;

//     ProcessCPUTimePackage process_pkg;
//     std::vector<double> process_cpu_list;

//     std::atomic_bool alive_;

//     std::condition_variable timer_;

//     std::mutex mu_;
// };

// CPUMeasurePackage::CPUMeasurePackage(size_t time_interval_ms) {
//   thread_ = new std::thread([&, time_interval_ms](){
//     std::unique_lock<std::mutex> lck(mu_);
//     while (alive_) {
//       timer_.wait_for(lck, std::chrono::milliseconds(time_interval_ms));

//     }
//   });
// }



#endif //#ifndef BENCHMARK_UTILS