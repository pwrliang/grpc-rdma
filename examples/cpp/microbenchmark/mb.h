#ifndef MICROBENCHMARK_MB_H
#define MICROBENCHMARK_MB_H
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>

#include "grpc/impl/codegen/log.h"
#define MAX_EVENTS 100

enum class Mode { kBusyPolling, kBusyPollingRR, kEvent, kBPEV, kTCP };
enum class Dir { kS2C, kC2S, kBi };

Mode parse_mode(const std::string& mode) {
  if (mode == "bp") {
    return Mode::kBusyPolling;
  } else if (mode == "bprr") {
    return Mode::kBusyPollingRR;
  } else if (mode == "event") {
    return Mode::kEvent;
  } else if (mode == "bpev") {
    return Mode::kBPEV;
  } else if (mode == "tcp") {
    return Mode::kTCP;
  }
  gpr_log(GPR_ERROR, "Invalid mode: %s", mode.c_str());
  exit(1);
}

std::string mode_to_string(Mode mode) {
  switch (mode) {
    case Mode::kBusyPolling:
      return "Busy Polling";
    case Mode::kBusyPollingRR:
      return "Busy Poling (RR)";
    case Mode::kEvent:
      return "Event";
    case Mode::kBPEV:
      return "Busy Polling (Event)";
    case Mode::kTCP:
      return "TCP";
  }
  return "";
}

Dir parse_dir(const std::string& dir) {
  if (dir == "s2c") {
    return Dir::kS2C;
  } else if (dir == "c2s") {
    return Dir::kC2S;
  } else if (dir == "bi") {
    return Dir::kBi;
  }
  gpr_log(GPR_ERROR, "Invalid dir: %s", dir.c_str());
  exit(1);
}

std::string dir_to_string(Dir dir) {
  switch (dir) {
    case Dir::kS2C:
      return "S2C";
    case Dir::kC2S:
      return "C2S";
    case Dir::kBi:
      return "Bi";
  }
  return "";
}

struct BenchmarkConfig {
  Mode mode{};
  Dir dir{};
  bool mpi_server{};
  bool affinity{};
  int n_max_polling_thread{};
  int n_client{};
  int n_batch{};
  int n_cpu_limit{};
  // for event mode
  int server_timeout{};
  int client_timeout{};
  int send_interval_us{};
  double n_work_thread{};
  bool should_work{};
  bool work_stealing{};
};

int bind_thread_to_cores(const std::vector<int>& core_ids) {
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);

  for (int core_id : core_ids) {
    if (core_id < 0 || core_id >= num_cores) {
      return EINVAL;
    }
    CPU_SET(core_id, &cpuset);
  }

  pthread_t current_thread = pthread_self();
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

int bind_thread_to_core(int core_id) { return bind_thread_to_cores({core_id}); }

ssize_t tcp_send1(int fd, const struct msghdr* msg, int additional_flags = 0) {
  ssize_t sent_length;
  do {
    sent_length = sendmsg(fd, msg, MSG_NOSIGNAL | additional_flags);
  } while (sent_length < 0 && errno == EINTR);
  return sent_length;
}

bool write_data(int fd, const char* local, const size_t sz) {
  size_t remain = sz;
  ssize_t done;

  while (remain) {
    done = ::write(fd, local + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
      } else {
        gpr_log(GPR_ERROR, "write failure, errno %d: %s", errno,
                strerror(errno));
        return false;
      }
    } else {
      remain -= done;
    }
  }
  return true;
}

bool read_data(int fd, char* remote, const size_t sz) {
  size_t remain = sz;
  ssize_t done;

  while (remain) {
    done = ::read(fd, remote + (sz - remain), remain);
    if (done < 0) {
      if (errno == EINTR || errno == EAGAIN) {
      } else {
        gpr_log(GPR_ERROR, "read failure, read errno %d: %s", errno,
                strerror(errno));
        return false;
      }
    } else {
      remain -= done;
    }
  }

  return true;
}
#endif  // MICROBENCHMARK_MB_H
