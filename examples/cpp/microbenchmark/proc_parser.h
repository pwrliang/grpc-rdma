#ifndef MICROBENCHMARK_PROC_PARSER_H
#define MICROBENCHMARK_PROC_PARSER_H
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

inline std::vector<uint64_t> read_counter(const char* path, const char* kw) {
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

inline std::map<std::string, std::vector<uint64_t>> read_counters_wild(
    const char* path, const char* kw) {
  std::map<std::string, std::vector<uint64_t>> matched_counters;
  std::ifstream is(path);
  std::string line;

  while (std::getline(is, line)) {
    auto it = line.find(kw);
    if (it != std::string::npos) {
      auto space_it = it;
      while (line.at(space_it) != ' ') {
        space_it++;
      }

      auto counter_name = line.substr(it, space_it - it);

      std::istringstream iss(line.substr(space_it));
      uint64_t count;
      std::vector<uint64_t> counts;

      while (iss >> count) {
        counts.push_back(count);
      }
      matched_counters[counter_name] = counts;
    }
  }
  is.close();
  return matched_counters;
}

struct cpu_time_t {
  double t_user;
  double t_nice;
  double t_system;
  double t_idle;
  double t_iowait;
  double t_irq;
  double t_softirq;
  double t_sum;

  cpu_time_t() = default;

  explicit cpu_time_t(const std::vector<uint64_t>& cpu_counters) {
    double tick = sysconf(_SC_CLK_TCK);

    t_user = cpu_counters[0] / tick;
    t_nice = cpu_counters[1] / tick;
    t_system = cpu_counters[2] / tick;
    t_idle = cpu_counters[3] / tick;
    t_iowait = cpu_counters[4] / tick;
    t_irq = cpu_counters[5] / tick;
    t_softirq = cpu_counters[6] / tick;
    t_sum = 0;
    for (int i = 0; i < 7; i++) {
      t_sum += cpu_counters[i];
    }
    t_sum /= tick;
  }

  cpu_time_t operator-(const cpu_time_t& rhs) {
    cpu_time_t t;

    t.t_user = t_user - rhs.t_user;
    t.t_nice = t_nice - rhs.t_nice;
    t.t_system = t_system - rhs.t_system;
    t.t_idle = t_idle - rhs.t_idle;
    t.t_iowait = t_iowait - rhs.t_iowait;
    t.t_irq = t_irq - rhs.t_irq;
    t.t_softirq = t_softirq - rhs.t_softirq;
    t.t_sum = t_sum - rhs.t_sum;

    return t;
  }
};

inline cpu_time_t get_cpu_time() {
  auto cpu_counters = read_counter("/proc/stat", "cpu ");
  cpu_time_t cpu_time(cpu_counters);

  return cpu_time;
}

inline std::map<std::string, cpu_time_t> get_cpu_time_per_core() {
  auto cpu_counters = read_counters_wild("/proc/stat", "cpu");
  std::map<std::string, cpu_time_t> r;

  for (auto& kv : cpu_counters) {
    auto cpu_id = kv.first;
    r[cpu_id] = cpu_time_t(kv.second);
  }

  return r;
}

inline std::vector<uint64_t> get_tasklet_count() {
  return read_counter("/proc/softirqs", "TASKLET:");
}

#endif  // MICROBENCHMARK_PROC_PARSER_H
