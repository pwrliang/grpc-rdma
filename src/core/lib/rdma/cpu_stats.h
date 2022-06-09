#ifndef GRPC_CORE_LIB_RDMA_CPU_STATS_H
#define GRPC_CORE_LIB_RDMA_CPU_STATS_H

#include <cstring>
#include <fstream>
#include <istream>
#include <sstream>
#include <vector>
enum CPUTimeCounter {
  kUserCounter = 0,
  kNiceCounter,
  kSysCounter,
  kIdleCounter,
  kIOWaitCounter,
  kIRQCounter,
  kSoftIRQCounter,
};

inline std::vector<uint64_t> read_counters(const char* path, const char* kw) {
  std::vector<uint64_t> counts;
  std::ifstream is(path);
  std::string line;

  while (std::getline(is, line)) {
    auto it = line.find(kw);
    if (it != std::string::npos) {
      std::istringstream iss(line.substr(it + strlen(kw)));
      uint64_t count;

      while (iss >> count) {
        counts.push_back(count);
      }
      break;
    }
  }
  is.close();
  return counts;
}

inline size_t get_idle_time(const std::vector<uint64_t>& counters) {
  return counters[kIdleCounter] + counters[kIOWaitCounter];
}

inline size_t get_active_time(const std::vector<uint64_t>& counters) {
  return counters[kUserCounter] + counters[kNiceCounter] +
         counters[kSysCounter] + counters[kIRQCounter] +
         counters[kSoftIRQCounter];
}

#endif  // GRPC_CORE_LIB_RDMA_CPU_STATS_H
