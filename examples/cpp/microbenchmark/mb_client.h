#ifndef MICROBENCHMARK_MB_CLIENT_H
#define MICROBENCHMARK_MB_CLIENT_H

#include <src/core/lib/iomgr/sys_epoll_wrapper.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "SockUtils.h"
#include "absl/time/time.h"
#include "flags.h"
#include "gflags/gflags.h"
#include "grpc/support/log.h"
#include "netinet/tcp.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"
#define MAX_EVENTS 100

int random(int min, int max) {
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}

class RDMAClient {
 public:
  RDMAClient(const CommSpec& comm_spec) : comm_spec_(comm_spec) {
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

    if (::connect(sockfd_, reinterpret_cast<struct sockaddr*>(&serv_addr),
                  sizeof(serv_addr)) < 0) {
      gpr_log(GPR_ERROR, "RDMAClient::connect, error on connect");
      exit(-1);
    }
  }

  void RunBusyPolling() {
    int64_t data_out = 123, data_in;
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
    MPI_Barrier(comm_spec_.comm());
    gpr_log(GPR_INFO, "client %d is connected to server",
            comm_spec_.worker_id());

    auto batch_size = FLAGS_batch;
    int warmup_round = FLAGS_warmup;

    absl::Time t_begin;
    for (int i = 0; i < batch_size + warmup_round; ++i) {
      if (i == warmup_round) {
        MPI_Barrier(comm_spec_.client_comm());
        t_begin = absl::Now();
      }
      while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
      }
      while (!rdmasr.check_incoming()) {
      }
      size_t unread_mlens = rdmasr.check_and_ack_incomings_locked();
      GPR_ASSERT(unread_mlens == sizeof(data_in));
      rdmasr.recv(&msghdr_in);
    }
    auto time = ToDoubleMicroseconds((absl::Now() - t_begin));
    gpr_log(GPR_INFO, "Latency: %lf micro", time / batch_size);
    // Notify the server that I'm finished
    data_out = -1;
    while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
    }
  }

  void RunEpoll() {
    RDMASenderReceiverEvent rdmasr;
    rdmasr.connect(sockfd_);
    rdmasr.WaitConnect();
    MPI_Barrier(comm_spec_.comm());
    gpr_log(GPR_INFO, "client %d is connected to server",
            comm_spec_.worker_id());

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

    int64_t data_out = 123, data_in;
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

    auto batch_size = FLAGS_batch;
    int warmup_round = FLAGS_warmup;
    epoll_event events[MAX_EVENTS];

    auto process_msg = [&](bool notify_exit) {
      auto mlen = rdmasr.check_and_ack_incomings_locked();
      if (mlen > 0) {
        GPR_ASSERT(mlen == sizeof(data_in));
        size_t read_bytes = rdmasr.recv(&msghdr_in);
        if (notify_exit) {
          data_out = -1;
        }
        GPR_ASSERT(rdmasr.send(&msghdr_out, sizeof(data_out)));
      }
    };

    absl::Time t_begin;
    // Send first msg
    GPR_ASSERT(rdmasr.send(&msghdr_out, sizeof(data_out)));

    for (int i = 0; i < batch_size + warmup_round; ++i) {
      if (i == warmup_round) {
        MPI_Barrier(comm_spec_.client_comm());
        t_begin = absl::Now();
      }

      int r;
      do {
        r = epoll_wait(epfd, events, MAX_EVENTS, -1);
      } while ((r < 0 && errno == EINTR) || r == 0);

      for (int j = 0; j < r; j++) {
        epoll_event& ev = events[j];
        void* data_ptr = ev.data.ptr;

        if ((reinterpret_cast<intptr_t>(data_ptr) & 1) ==
            1) {  // pollable_add_rdma_channel_fd
          auto* rdmasr = reinterpret_cast<RDMASenderReceiverEvent*>(
              ~static_cast<intptr_t>(1) &
              reinterpret_cast<intptr_t>(ev.data.ptr));

          if ((ev.events & EPOLLIN) != 0) {
            GPR_ASSERT(rdmasr != nullptr);
            rdmasr->check_metadata();
            process_msg(i == batch_size + warmup_round - 1);
          }
        } else if ((reinterpret_cast<intptr_t>(data_ptr) & 2) == 2) {
          auto* rdmasr = reinterpret_cast<RDMASenderReceiverEvent*>(
              ~static_cast<intptr_t>(2) &
              reinterpret_cast<intptr_t>(ev.data.ptr));
          if ((ev.events & EPOLLIN) != 0) {
            GPR_ASSERT(rdmasr != nullptr);
            rdmasr->check_data();
            process_msg(i == batch_size + warmup_round - 1);
          }
        }
      }
    }
    auto time = ToDoubleMicroseconds((absl::Now() - t_begin));
    gpr_log(GPR_INFO, "Latency: %lf micro", time / batch_size);
  }

 private:
  CommSpec comm_spec_;
  int sockfd_;
};

#endif  // MICROBENCHMARK_MB_CLIENT_H