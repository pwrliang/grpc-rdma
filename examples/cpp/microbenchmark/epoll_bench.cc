#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "absl/time/clock.h"
#include "flags.h"
#include "grpc/support/log.h"
#define MAX_EPOLL_EVENTS 100
std::mutex mutex;
std::condition_variable cv;
bool ready = false;
bool running = true;

struct epoll_fd {
  int epfd;
  epoll_fd() { epfd = epoll_create1(EPOLL_CLOEXEC); }
  ~epoll_fd() { close(epfd); }
};

struct event_fd {
  int read_fd;
  event_fd() { read_fd = eventfd(0, EFD_NONBLOCK); }
  ~event_fd() { close(read_fd); }
};

std::shared_ptr<event_fd> create_eventfd(epoll_fd* epfd, void* ptr = nullptr) {
  auto fd = std::make_shared<event_fd>();

  epoll_event ev{};
  ev.events = static_cast<uint32_t>(EPOLLIN);
  if (ptr != nullptr) {
    ev.data.ptr = ptr;
  } else {
    ev.data.fd = fd->read_fd;
  }

  if (epoll_ctl(epfd->epfd, EPOLL_CTL_ADD, fd->read_fd, &ev) != 0) {
    gpr_log(GPR_ERROR, "add_eventfd failed: %s", strerror(errno));
    exit(1);
  }
  return fd;
}

struct WorkerResource {
  int worker_id;
  uint32_t nops;
  std::shared_ptr<epoll_fd> epfd;
  std::shared_ptr<event_fd> c2s_fd;
  std::vector<std::shared_ptr<event_fd>> s_to_c_fds;  // only valid for server
  int timeout;
  bool rw;

  explicit WorkerResource(int wid) : worker_id(wid), nops(0) {
    if (worker_id == 0) {
      timeout = FLAGS_server_timeout;
    } else {
      timeout = FLAGS_client_timeout;
    }

    epfd = std::make_shared<epoll_fd>();
    rw = FLAGS_rw;
  }
};

void server_worker(WorkerResource* server_res) {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return ready; });
  lock.unlock();

  epoll_event events[MAX_EPOLL_EVENTS];
  ssize_t sz;
  int64_t val = 1;

  auto bcast = [&]() {
    // write to every worker
    for (int i = 1; i <= FLAGS_nclient; i++) {
      do {
        sz = write(server_res->s_to_c_fds[i]->read_fd, &val, sizeof(val));
      } while (running && sz < 0 && errno == EAGAIN);
    }
  };

  while (running) {
    if (server_res->rw) {
      int r;
      do {
        r = epoll_wait(server_res->epfd->epfd, events, MAX_EPOLL_EVENTS,
                       server_res->timeout);
      } while ((r < 0 && errno == EINTR) || r == 0);
      if (r < 0) {
        gpr_log(GPR_ERROR, "epoll error: %s", strerror(errno));
        exit(1);
      }
      for (int i = 0; i < r; i++) {
        auto& ev = events[i];

        if (ev.events & EPOLLIN) {
          WorkerResource* cli_res = static_cast<WorkerResource*>(ev.data.ptr);
          do {
            sz = read(cli_res->c2s_fd->read_fd, &val, sizeof(val));
          } while (running && sz < 0 && errno == EAGAIN);

          do {
            sz = write(server_res->s_to_c_fds[cli_res->worker_id]->read_fd,
                       &val, sizeof(val));
          } while (running && sz < 0 && errno == EAGAIN);
        }
      }
      server_res->nops++;
    } else {
      bcast();
    }
  }

  bcast();
}

void client_worker(WorkerResource* cli_res) {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return ready; });
  lock.unlock();

  epoll_event events[MAX_EPOLL_EVENTS];

  int64_t val = 1;

  auto send_to = [&]() {
    if (cli_res->rw) {
      ssize_t sz;
      do {
        sz = write(cli_res->c2s_fd->read_fd, &val, sizeof(val));
      } while (running && sz < 0 && errno == EAGAIN);
    }
  };

  send_to();
  while (running) {
    int r;
    do {
      r = epoll_wait(cli_res->epfd->epfd, events, MAX_EPOLL_EVENTS,
                     cli_res->timeout);
    } while ((r < 0 && errno == EINTR) || r == 0);

    if (r < 0) {
      gpr_log(GPR_ERROR, "epoll error: %s", strerror(errno));
    }

    for (int i = 0; i < r; i++) {
      auto& ev = events[i];

      if (ev.events & EPOLLIN) {
        int fd = ev.data.fd;
        int sz;
        do {
          sz = read(fd, &val, sizeof(val));
        } while (running && sz < 0 && errno == EAGAIN);
      }
    }
    send_to();

    cli_res->nops++;
  }

  // Notify server to exit
  send_to();
}

void Run() {
  std::vector<WorkerResource> res;
  std::vector<std::thread> ths;

  if (FLAGS_nclient <= 0) {
    gpr_log(GPR_ERROR, "We need at least 1 client");
    exit(1);
  }

  for (int i = 0; i <= FLAGS_nclient; i++) {
    res.emplace_back(i);
  }

  auto& server_res = res[0];
  // C2S
  for (int i = 1; i <= FLAGS_nclient; i++) {
    auto& cli_res = res[i];

    cli_res.c2s_fd = create_eventfd(server_res.epfd.get(), &cli_res);
  }

  // S2C
  server_res.s_to_c_fds.resize(FLAGS_nclient + 1);
  for (int i = 1; i <= FLAGS_nclient; i++) {
    auto& cli_res = res[i];
    server_res.s_to_c_fds[i] = create_eventfd(cli_res.epfd.get());
  }

  for (int i = 0; i <= FLAGS_nclient; i++) {
    if (i == 0) {
      ths.emplace_back(server_worker, &res[i]);
    } else {
      ths.emplace_back(client_worker, &res[i]);
    }
  }

  {
    std::lock_guard<std::mutex> lk(mutex);
    ready = true;
  }

  cv.notify_all();

  auto t_begin = absl::Now();
  sleep(FLAGS_runtime);
  running = false;

  for (auto& th : ths) {
    th.join();
  }
  auto t_duration = ToDoubleMicroseconds((absl::Now() - t_begin));

  for (int i = 0; i <= FLAGS_nclient; i++) {
    if (i == 0 && !FLAGS_rw) {  // skip writer thread
      continue;
    }
    auto& worker_res = res[i];
    gpr_log(GPR_INFO, "Thread %d, nops: %d Latency: %lf micro", i,
            worker_res.nops, t_duration / worker_res.nops);
  }
}

int main(int argc, char* argv[]) {
  setenv("GRPC_VERBOSITY", "INFO", 1);
  gpr_log_verbosity_init();

  gflags::SetUsageMessage(
      "Usage: mpiexec [mpi_opts] ./epoll_bench [main_opts]");
  if (argc == 1) {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "epoll_bench");
    exit(1);
  }
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::ShutDownCommandLineFlags();

  Run();

  return 0;
}
