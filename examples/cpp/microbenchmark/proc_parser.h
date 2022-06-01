#ifndef MICROBENCHMARK_PROC_PARSER_H
#define MICROBENCHMARK_PROC_PARSER_H
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

inline std::vector<int> read_counter(const char* path, const char* kw) {
  std::vector<int> counts;
  std::ifstream is(path);
  std::string line;

  while (std::getline(is, line)) {
    auto it = line.find(kw);
    if (it != std::string::npos) {
      std::istringstream iss(line.substr(it + strlen(kw)));
      int count;
      while (iss >> count) {
        counts.push_back(count);
      }
      break;
    }
  }
  is.close();
  return counts;
}

struct cpu_time_t {
  double t_user;
  double t_nice;
  double t_system;
  double t_idle;
  double t_iowait;
  double t_irq;
  double t_softirq;

  cpu_time_t operator-(const cpu_time_t& rhs) {
    cpu_time_t t;

    t.t_user = t_user - rhs.t_user;
    t.t_nice = t_nice - rhs.t_nice;
    t.t_system = t_system - rhs.t_system;
    t.t_idle = t_idle - rhs.t_idle;
    t.t_iowait = t_iowait - rhs.t_iowait;
    t.t_irq = t_irq - rhs.t_irq;
    t.t_softirq = t_softirq - rhs.t_softirq;

    return t;
  }
};

inline cpu_time_t get_cpu_time() {
  double tick = sysconf(_SC_CLK_TCK);
  auto cpu_counters = read_counter("/proc/stat", "cpu ");
  cpu_time_t cpu_time;

  cpu_time.t_user = cpu_counters[0] / tick;
  cpu_time.t_nice = cpu_counters[1] / tick;
  cpu_time.t_system = cpu_counters[2] / tick;
  cpu_time.t_idle = cpu_counters[3] / tick;
  cpu_time.t_iowait = cpu_counters[4] / tick;
  cpu_time.t_irq = cpu_counters[5] / tick;
  cpu_time.t_softirq = cpu_counters[6] / tick;
  return cpu_time;
}

inline std::vector<int> get_tasklet_count() {
  return read_counter("/proc/softirqs", "TASKLET:");
}

#endif  // MICROBENCHMARK_PROC_PARSER_H
