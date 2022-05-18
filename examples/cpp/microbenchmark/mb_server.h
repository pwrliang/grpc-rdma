#ifndef MICROBENCHMARK_MB_SERVER_H
#define MICROBENCHMARK_MB_SERVER_H

#include <netinet/tcp.h>
#include <src/core/lib/iomgr/sys_epoll_wrapper.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include "SockUtils.h"
#include "get_clock.h"
#include "mb.h"
#include "src/core/lib/rdma/RDMAConn.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"

#define MAX_CONN_NUM 1024
std::condition_variable cv;
std::mutex cv_m;
bool all_connected = false;

struct Connections {
  Connections() : epfd(epoll_create1(EPOLL_CLOEXEC)), tid(0), epoll_cycles(0) {}

  void CreateBPConnection(int fd) {
    auto conn = std::make_shared<RDMASenderReceiverBP>();
    conn->connect(fd);
    rdma_conns.push_back(conn);
  }

  void CreateEventConnection(int fd) {
    auto conn = std::make_shared<RDMASenderReceiverEvent>();
    conn->connect(fd);
    rdma_conns.push_back(conn);

    int rdma_meta_recv_channel_fd = conn->get_metadata_recv_channel_fd();
    struct epoll_event meta_recv_ev_fd;
    meta_recv_ev_fd.events =
        static_cast<uint32_t>(EPOLLIN | EPOLLET | EPOLLEXCLUSIVE);
    meta_recv_ev_fd.data.ptr =
        reinterpret_cast<void*>(reinterpret_cast<intptr_t>(conn.get()) | 1);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, rdma_meta_recv_channel_fd,
                  &meta_recv_ev_fd) != 0) {
      switch (errno) {
        case EEXIST:
          break;
        default:
          gpr_log(GPR_ERROR, "epoll_ctl error, errno: %d", errno);
      }
    }

    int rdma_recv_channel_fd = conn->get_recv_channel_fd();
    struct epoll_event recv_ev_fd;
    recv_ev_fd.events =
        static_cast<uint32_t>(EPOLLIN | EPOLLET | EPOLLEXCLUSIVE);
    recv_ev_fd.data.ptr =
        reinterpret_cast<void*>(reinterpret_cast<intptr_t>(conn.get()) | 2);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, rdma_recv_channel_fd, &recv_ev_fd) !=
        0) {
      switch (errno) {
        case EEXIST:
          break;
        default:
          gpr_log(GPR_ERROR, "epoll_ctl error, errno: %d", errno);
      }
    }
  }

  void BindCore() {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    bind_thread_to_core(tid % num_cores);
  }

  CommSpec comm_spec;
  std::vector<std::shared_ptr<RDMASenderReceiver>> rdma_conns;
  int epfd;
  int tid;
  cycles_t epoll_cycles;
  struct rusage rusage;
};

void serve_bp(Connections* conns, const BenchmarkConfig* config) {
  int64_t data_in = 0, data_out = 0;
  iovec iov_in, iov_out;
  msghdr msghdr_in, msghdr_out;

  iov_in.iov_base = &data_in;
  iov_in.iov_len = sizeof(data_in);
  iov_out.iov_base = &data_out;
  iov_out.iov_len = sizeof(data_out);

  msghdr_in.msg_iov = &iov_in;
  msghdr_in.msg_iovlen = 1;
  msghdr_out.msg_iov = &iov_out;
  msghdr_out.msg_iovlen = 1;

  if (config->affinity) {
    conns->BindCore();
  }

  {
    std::unique_lock<std::mutex> lk(cv_m);
    cv.wait(lk, [] { return all_connected; });
  }
  auto t_begin = absl::Now();
  uint32_t nops = 0;

  if (config->dir == Dir::kBi || config->dir == Dir::kC2S) {
    size_t n_alive_conn = conns->rdma_conns.size();

    while (n_alive_conn > 0) {
      for (int i = 0; i < conns->rdma_conns.size(); i++) {
        auto* rdmasr =
            dynamic_cast<RDMASenderReceiverBP*>(conns->rdma_conns[i].get());

        if (rdmasr != nullptr && rdmasr->check_incoming()) {
          size_t rest_mlen = rdmasr->check_and_ack_incomings_locked();

          while (rest_mlen > 0) {
            rest_mlen -= rdmasr->recv(&msghdr_in);
            nops++;

            if (data_in == -1) {  // release connection
              conns->rdma_conns[i] = nullptr;
              n_alive_conn--;
              GPR_ASSERT(rest_mlen == 0);
              continue;
            }

            if (config->dir == Dir::kBi) {
              data_out = data_in + 1;
              while (!rdmasr->send(&msghdr_out, sizeof(data_out))) {
              }
            }
          }
        }
      }
    }
  } else {
    for (int i = 0; i < config->n_batch; i++) {
      for (auto& rdmasr : conns->rdma_conns) {
        data_out = data_in + 1;
        if (i == config->n_batch - 1) {
          data_out = -1;
        }

        while (!rdmasr->send(&msghdr_out, sizeof(data_out))) {
        }
        nops++;
      }
    }
  }

  auto t_end = absl::Now();
  getrusage(RUSAGE_THREAD, &conns->rusage);

  if (nops > 1) {
    gpr_log(GPR_INFO,
            "Server thread: %d, Served Reqs: %u Avg Time: %lf nvcsw: %ld "
            "nivcsw: %ld",
            conns->tid, nops, ToDoubleMicroseconds((t_end - t_begin)) / nops,
            conns->rusage.ru_nvcsw, conns->rusage.ru_nivcsw);
  }
}

void serve_event(Connections* conns, const BenchmarkConfig* config) {
  int epfd = conns->epfd;
  epoll_event events[MAX_EVENTS];

  int64_t data_in = 0, data_out = 0;
  iovec iov_in, iov_out;
  msghdr msghdr_in, msghdr_out;

  iov_in.iov_base = &data_in;
  iov_in.iov_len = sizeof(data_in);
  iov_out.iov_base = &data_out;
  iov_out.iov_len = sizeof(data_out);

  msghdr_in.msg_iov = &iov_in;
  msghdr_in.msg_iovlen = 1;
  msghdr_out.msg_iov = &iov_out;
  msghdr_out.msg_iovlen = 1;

  if (config->affinity) {
    conns->BindCore();
  }

  {
    std::unique_lock<std::mutex> lk(cv_m);
    cv.wait(lk, [] { return all_connected; });
  }
  auto t_begin = absl::Now();
  uint32_t nops = 0;
  auto send_msg = [&](RDMASenderReceiverEvent* rdmasr) {
    while (!rdmasr->send(&msghdr_out, sizeof(data_out))) {
      rdmasr->check_metadata();
      rdmasr->check_and_ack_incomings_locked();
    }
  };

  if (config->dir == Dir::kBi || config->dir == Dir::kC2S) {
    size_t n_alive_conn = conns->rdma_conns.size();

    auto process_event = [&](RDMASenderReceiverEvent* rdmasr) {
      auto rest_mlen = rdmasr->check_and_ack_incomings_locked();

      while (rest_mlen > 0) {
        rest_mlen -= rdmasr->recv(&msghdr_in);
        nops++;

        if (data_in == -1) {  // Disconnect
          for (int i = 0; i < conns->rdma_conns.size(); i++) {
            if (conns->rdma_conns[i].get() == rdmasr) {
              conns->rdma_conns[i] = nullptr;
              n_alive_conn--;
              GPR_ASSERT(rest_mlen == 0);
              break;
            }
          }
          return;
        }

        if (config->dir == Dir::kBi) {
          data_out = data_in + 1;
          send_msg(rdmasr);
        }
      }
    };

    while (n_alive_conn > 0) {
      int r;
      cycles_t c1 = get_cycles();
      do {
        r = epoll_wait(epfd, events, MAX_EVENTS, config->server_timeout);
      } while ((r < 0 && errno == EINTR) || r == 0);
      cycles_t c2 = get_cycles();
      conns->epoll_cycles += c2 - c1;

      for (int i = 0; i < r; i++) {
        epoll_event& ev = events[i];
        void* data_ptr = ev.data.ptr;

        if ((reinterpret_cast<intptr_t>(data_ptr) & 1) == 1) {
          auto* rdmasr = reinterpret_cast<RDMASenderReceiverEvent*>(
              ~static_cast<intptr_t>(1) &
              reinterpret_cast<intptr_t>(ev.data.ptr));

          if ((ev.events & EPOLLIN) != 0) {
            GPR_DEBUG_ASSERT(rdmasr != nullptr);
            rdmasr->check_metadata();
            process_event(rdmasr);
          }
        } else if ((reinterpret_cast<intptr_t>(data_ptr) & 2) == 2) {
          auto* rdmasr = reinterpret_cast<RDMASenderReceiverEvent*>(
              ~static_cast<intptr_t>(2) &
              reinterpret_cast<intptr_t>(ev.data.ptr));
          if ((ev.events & EPOLLIN) != 0) {
            GPR_DEBUG_ASSERT(rdmasr != nullptr);
            rdmasr->check_data();
            process_event(rdmasr);
          }
        }
      }
    }
  } else if (config->dir == Dir::kS2C) {
    // TODO: selective send
    GPR_ASSERT(config->n_active_client <= 0);
    for (int i = 0; i < config->n_batch; i++) {
      for (auto& rdmasr : conns->rdma_conns) {
        data_out = data_in + 1;
        if (i == config->n_batch - 1) {
          data_out = -1;
        }

        send_msg(dynamic_cast<RDMASenderReceiverEvent*>(rdmasr.get()));
        nops++;
      }
    }
  }

  auto t_end = absl::Now();
  getrusage(RUSAGE_THREAD, &conns->rusage);

  if (nops > 1) {
    gpr_log(GPR_INFO,
            "Server thread: %d, Served Reqs: %u Avg Time: %lf nvcsw: %ld "
            "nivcsw: %ld",
            conns->tid, nops, ToDoubleMicroseconds((t_end - t_begin)) / nops,
            conns->rusage.ru_nvcsw, conns->rusage.ru_nivcsw);
  }
}

void compute(std::atomic_bool* running) {
  {
    std::unique_lock<std::mutex> lk(cv_m);
    cv.wait(lk, [] { return all_connected; });
  }

  while (running->load())
    ;
}

class RDMAServer {
 public:
  explicit RDMAServer(const BenchmarkConfig& config, const CommSpec& comm_spec)
      : config_(config), comm_spec_(comm_spec) {
    sockfd_ = SocketUtils::socket(AF_INET, SOCK_STREAM, 0);
    int n_polling_thread = config_.n_max_polling_thread;

    if (config.mode == Mode::kBusyPolling) {
      if (config.mpi_server) {
        n_polling_thread = comm_spec.worker_num() - 1;
      } else {
        n_polling_thread = config.n_client;
      }
    }
    connections_per_thread_.resize(n_polling_thread);

    for (int i = 0; i < n_polling_thread; i++) {
      connections_per_thread_[i].tid = i;
      connections_per_thread_[i].comm_spec = comm_spec_;
      if (config.mode == Mode::kBusyPolling ||
          config.mode == Mode::kBusyPollingRR) {
        polling_threads_.emplace_back(&serve_bp, &connections_per_thread_[i],
                                      &config);
      } else if (config.mode == Mode::kEvent) {
        polling_threads_.emplace_back(&serve_event, &connections_per_thread_[i],
                                      &config);
      }
    }

    computing_ = true;
    for (int i = 0; i < FLAGS_computing_thread; i++) {
      computing_threads_.emplace_back(&compute, &computing_);
    }

    int opt = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                   sizeof(opt))) {
      gpr_log(GPR_ERROR,
              "RDMAServer::RDMAServer, error on setsockopt (SO_REUSEADDR)");
      exit(-1);
    }
  }

  void start(int port) {
    sockaddr_in server_addr;
    bzero(&server_addr, sizeof(sockaddr_in));
    SocketUtils::setAddr(server_addr, port);

    if (::bind(sockfd_, reinterpret_cast<sockaddr*>(&server_addr),
               sizeof(server_addr)) < 0) {
      gpr_log(GPR_ERROR, "RDMAServer::start, error on bind");
      exit(-1);
    }

    if (::listen(sockfd_, MAX_CONN_NUM) < 0) {
      gpr_log(GPR_ERROR, "RDMAServer::start, error on listen");
      exit(-1);
    }

    gpr_log(GPR_INFO,
            "RDMA Server is listening on %d, mode: %s, "
            "polling thread: %zu, computing thread: %zu",
            port, mode_to_string(config_.mode).c_str(), polling_threads_.size(),
            computing_threads_.size());

    sockaddr client_sockaddr;
    socklen_t addr_len = sizeof(client_sockaddr);
    int client_id = 0;
    int n_client;

    if (config_.mpi_server) {
      // Wait for listening
      MPI_Barrier(comm_spec_.comm());
      n_client = comm_spec_.worker_num() - 1;
    } else {
      n_client = config_.n_client;
    }

    while (client_id < n_client) {
      int newsd = accept(sockfd_, &client_sockaddr, &addr_len);
      if (newsd < 0) {
        gpr_log(GPR_ERROR, "RDMAServer::start, error on accept");
        exit(-1);
      }

      auto* addr_in = reinterpret_cast<sockaddr_in*>(&client_sockaddr);
      char* s = inet_ntoa(addr_in->sin_addr);
      //      printf("IP address: %s, client id: %d\n", s, client_id);
      int flag = 1;
      setsockopt(newsd, SOL_TCP, TCP_NODELAY, &flag, sizeof(flag));
      dispatch(newsd, client_id);
      client_id++;
      if (client_id % 10 == 0) {
        gpr_log(GPR_INFO, "There are %d clients are connected", client_id);
      }
    }

    for (auto& conns : connections_per_thread_) {
      for (auto& rdmasr : conns.rdma_conns) {
        rdmasr->WaitConnect();
      }
    }

    gpr_log(GPR_INFO, "%d clients are connected", n_client);

    // Waiting for all clients connected
    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }
    {
      // notify serving threads
      std::lock_guard<std::mutex> lk(cv_m);
      all_connected = true;
      cv.notify_all();
    }
    for (auto& th : polling_threads_) {
      th.join();
    }

    computing_ = false;
    for (auto& th : computing_threads_) {
      th.join();
    }
  }

 private:
  BenchmarkConfig config_;
  CommSpec comm_spec_;
  int sockfd_{};
  std::vector<std::thread> polling_threads_;
  std::vector<std::thread> computing_threads_;
  std::vector<Connections> connections_per_thread_;
  std::atomic_bool computing_;

  void dispatch(int fd, int conn_id) {
    int work_thread_id = conn_id % polling_threads_.size();

    if (config_.mode == Mode::kBusyPolling ||
        config_.mode == Mode::kBusyPollingRR) {
      connections_per_thread_[work_thread_id].CreateBPConnection(fd);
    } else if (config_.mode == Mode::kEvent) {
      connections_per_thread_[work_thread_id].CreateEventConnection(fd);
    }
  }
};

#endif  // MICROBENCHMARK_MB_SERVER_H