#ifndef MICROBENCHMARK_MB_CLIENT_H
#define MICROBENCHMARK_MB_CLIENT_H

#include <src/core/lib/iomgr/sys_epoll_wrapper.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "SockUtils.h"
#include "absl/time/time.h"
#include "get_clock.h"
#include "gflags/gflags.h"
#include "grpc/support/log.h"
#include "netinet/tcp.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"

class RDMAClient {
 public:
  RDMAClient(const BenchmarkConfig& config, const CommSpec& comm_spec)
      : config_(config), comm_spec_(comm_spec) {
    sockfd_ = SocketUtils::socket(AF_INET, SOCK_STREAM, 0);
  }

  void connect(const char* server_address, int port) {
    struct sockaddr_in serv_addr;
    char serv_ip[16];
    strcpy(serv_ip, server_address);
    bzero(&serv_addr, sizeof(serv_addr));
    SocketUtils::setAddr(serv_addr, serv_ip, port);

    int flag = 1;
    if (setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&flag), sizeof(flag))) {
      gpr_log(GPR_ERROR,
              "RDMAClient::connect, error on setsockopt (TCP_NODELAY)");
      exit(-1);
    }
    // Wait for listening
    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }

    int r;
    do {
      r = ::connect(sockfd_, reinterpret_cast<struct sockaddr*>(&serv_addr),
                    sizeof(serv_addr));

      if (r < 0) {
        char hn[255];
        int hn_len;
        MPI_Get_processor_name(hn, &hn_len);
        gpr_log(GPR_ERROR, "Client %s cannot connect to server %s, errno: %d",
                hn, server_address, errno);
        sleep(1);
      }
    } while (r < 0);
  }

  void RunBusyPolling() {
    int64_t data_out = 0, data_in = 0;
    msghdr msghdr_out, msghdr_in;
    iovec iov_out, iov_in;
    iov_out.iov_base = &data_out;
    iov_out.iov_len = sizeof(data_out);
    iov_in.iov_base = &data_in;
    iov_in.iov_len = sizeof(data_in);
    msghdr_out.msg_iov = &iov_out;
    msghdr_out.msg_iovlen = 1;
    msghdr_in.msg_iov = &iov_in;
    msghdr_in.msg_iovlen = 1;

    RDMASenderReceiverBP rdmasr;
    rdmasr.connect(sockfd_);
    rdmasr.WaitConnect();

    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }

    if (config_.affinity) {
      int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
      bind_thread_to_core(comm_spec_.local_id() % num_cores);
    }

    int warmup_round = config_.n_warmup;
    auto batch_size = config_.n_batch;
    bool should_work = config_.n_active_client <= 0 ||
                       (comm_spec_.worker_id() <=
                        config_.n_active_client - (config_.mpi_server ? 0 : 1));
    absl::Time t_begin;
    cycles_t poll_cycles = 0;

    auto send_msg = [&] {
      while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
      }
    };

    for (int i = 0; i < batch_size + warmup_round; ++i) {
      if (i == warmup_round) {
        MPI_Barrier(comm_spec_.client_comm());
        t_begin = absl::Now();
      }

      if (i == 0 && config_.dir == Dir::kBi || config_.dir == Dir::kC2S) {
        send_msg();
      }

      if (config_.dir == Dir::kBi || config_.dir == Dir::kS2C) {
        cycles_t c1 = get_cycles();
        while (!rdmasr.check_incoming()) {
        }
        cycles_t c2 = get_cycles();

        if (i >= warmup_round) {
          poll_cycles += (c2 - c1);
        }

        size_t mlens = rdmasr.check_and_ack_incomings_locked();
        while (mlens > 0) {
          size_t actual_size = rdmasr.recv(&msghdr_in);

          // Server notifies client to exit
          if (config_.dir == Dir::kS2C && data_in == -1) {
            goto finish;
          }

          if (config_.dir == Dir::kBi) {
            send_msg();
          }
          mlens -= actual_size;
        }
      }
    }

  finish:
    // Notify server to exit
    if (config_.dir == Dir::kBi || config_.dir == Dir::kC2S) {
      data_out = -1;
      send_msg();
    }

    rusage rusage;
    getrusage(RUSAGE_THREAD, &rusage);
    auto time = ToDoubleMicroseconds((absl::Now() - t_begin));

    std::stringstream ss;
    ss << "Rank: " << comm_spec_.worker_id()
       << " Latency: " << time / batch_size;

    if (config_.dir == Dir::kBi || config_.dir == Dir::kS2C) {
      int no_cpu_freq_fail = 0;
      double mhz = get_cpu_mhz(no_cpu_freq_fail);

      ss << " Poll Time: " << poll_cycles / mhz / batch_size;
    }
    ss << " nvcsw: " << rusage.ru_nvcsw << " nivscw: " << rusage.ru_nivcsw;
    gpr_log(GPR_INFO, "%s", ss.str().c_str());
    MPI_Barrier(comm_spec_.client_comm());
  }

  void RunEpoll() {
    RDMASenderReceiverEvent rdmasr;
    rdmasr.connect(sockfd_);
    rdmasr.WaitConnect();
    // Waiting for all clients connected
    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }

    if (config_.affinity) {
      int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
      bind_thread_to_core(comm_spec_.local_id() % num_cores);
    }

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int rdma_meta_recv_channel_fd = rdmasr.get_metadata_recv_channel_fd();
    struct epoll_event meta_recv_ev_fd;
    meta_recv_ev_fd.events =
        static_cast<uint32_t>(EPOLLIN | EPOLLET | EPOLLEXCLUSIVE);
    meta_recv_ev_fd.data.ptr =
        reinterpret_cast<void*>(reinterpret_cast<intptr_t>(&rdmasr) | 1);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, rdma_meta_recv_channel_fd,
                  &meta_recv_ev_fd) != 0) {
      switch (errno) {
        case EEXIST:
          break;
        default:
          gpr_log(GPR_ERROR, "epoll_ctl error, errno: %d", errno);
      }
    }

    int rdma_recv_channel_fd = rdmasr.get_recv_channel_fd();
    struct epoll_event recv_ev_fd;
    recv_ev_fd.events =
        static_cast<uint32_t>(EPOLLIN | EPOLLET | EPOLLEXCLUSIVE);
    recv_ev_fd.data.ptr =
        reinterpret_cast<void*>(reinterpret_cast<intptr_t>(&rdmasr) | 2);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, rdma_recv_channel_fd, &recv_ev_fd) !=
        0) {
      switch (errno) {
        case EEXIST:
          break;
        default:
          gpr_log(GPR_ERROR, "epoll_ctl error, errno: %d", errno);
      }
    }

    int64_t data_out = 0, data_in = 0;
    msghdr msghdr_out, msghdr_in;
    iovec iov_out, iov_in;
    iov_out.iov_base = &data_out;
    iov_out.iov_len = sizeof(data_out);
    iov_in.iov_base = &data_in;
    iov_in.iov_len = sizeof(data_in);
    msghdr_out.msg_iov = &iov_out;
    msghdr_out.msg_iovlen = 1;
    msghdr_in.msg_iov = &iov_in;
    msghdr_in.msg_iovlen = 1;

    auto warmup_round = config_.n_warmup;
    auto batch_size = config_.n_batch;
    epoll_event events[MAX_EVENTS];
    auto send_msg = [&] {
      while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
        rdmasr.check_metadata();
        rdmasr.check_and_ack_incomings_locked();
      }
    };

    auto process_event = [&](RDMASenderReceiverEvent* rdmasr) {
      bool c_should_exit = false;
      auto rest_mlen = rdmasr->check_and_ack_incomings_locked();

      while (rest_mlen > 0) {
        rest_mlen -= rdmasr->recv(&msghdr_in);

        if (config_.dir == Dir::kS2C && data_in == -1) {
          c_should_exit = true;
        }

        if (config_.dir == Dir::kBi) {
          send_msg();
        }
      }

      return c_should_exit;
    };

    absl::Time t_begin;
    cycles_t epoll_cycles = 0;

    for (int i = 0; config_.dir == Dir::kS2C ||
                    ((config_.dir == Dir::kBi || config_.dir == Dir::kC2S) &&
                     i < batch_size + warmup_round);
         ++i) {
      if (i == warmup_round) {
        MPI_Barrier(comm_spec_.client_comm());
        t_begin = absl::Now();
      }

      if (i == 0 && config_.dir == Dir::kBi || config_.dir == Dir::kC2S) {
        send_msg();
      }

      if (config_.dir == Dir::kBi || config_.dir == Dir::kS2C) {
        int r;
        cycles_t c1 = get_cycles();
        do {
          r = epoll_wait(epfd, events, MAX_EVENTS, config_.client_timeout);
        } while ((r < 0 && errno == EINTR) || r == 0);
        cycles_t c2 = get_cycles();

        if (i >= warmup_round) {
          epoll_cycles += c2 - c1;
        }

        for (int j = 0; j < r; j++) {
          epoll_event& ev = events[j];
          void* data_ptr = ev.data.ptr;

          if ((reinterpret_cast<intptr_t>(data_ptr) & 1) ==
              1) {  // pollable_add_rdma_channel_fd
            auto* rdmasr = reinterpret_cast<RDMASenderReceiverEvent*>(
                ~static_cast<intptr_t>(1) &
                reinterpret_cast<intptr_t>(ev.data.ptr));

            if ((ev.events & EPOLLIN) != 0) {
              GPR_DEBUG_ASSERT(rdmasr != nullptr);
              rdmasr->check_metadata();
              if (process_event(rdmasr)) {
                goto finish;
              }
            }
          } else if ((reinterpret_cast<intptr_t>(data_ptr) & 2) == 2) {
            auto* rdmasr = reinterpret_cast<RDMASenderReceiverEvent*>(
                ~static_cast<intptr_t>(2) &
                reinterpret_cast<intptr_t>(ev.data.ptr));
            if ((ev.events & EPOLLIN) != 0) {
              GPR_DEBUG_ASSERT(rdmasr != nullptr);
              rdmasr->check_data();
              if (process_event(rdmasr)) {
                goto finish;
              }
            }
          }
        }
      }
    }
  finish:
    printf("Cient exit\n");
    // Notify server to exit
    if (config_.dir == Dir::kBi || config_.dir == Dir::kC2S) {
      data_out = -1;
      send_msg();
    }

    rusage rusage;
    getrusage(RUSAGE_THREAD, &rusage);

    auto time = ToDoubleMicroseconds((absl::Now() - t_begin));

    std::stringstream ss;
    ss << "Rank: " << comm_spec_.worker_id()
       << " Latency: " << time / batch_size;

    if (config_.dir == Dir::kBi || config_.dir == Dir::kS2C) {
      int no_cpu_freq_fail = 0;
      double mhz = get_cpu_mhz(no_cpu_freq_fail);

      ss << " Poll Time: " << epoll_cycles / mhz / batch_size;
    }
    ss << " nvcsw: " << rusage.ru_nvcsw << " nivscw: " << rusage.ru_nivcsw;
    gpr_log(GPR_INFO, "%s", ss.str().c_str());
    MPI_Barrier(comm_spec_.client_comm());
  }

 private:
  BenchmarkConfig config_;
  CommSpec comm_spec_;
  int sockfd_;
};

#endif  // MICROBENCHMARK_MB_CLIENT_H