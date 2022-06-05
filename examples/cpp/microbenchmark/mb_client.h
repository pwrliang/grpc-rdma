#ifndef MICROBENCHMARK_MB_CLIENT_H
#define MICROBENCHMARK_MB_CLIENT_H
#include <src/core/lib/iomgr/sys_epoll_wrapper.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <queue>
#include "SockUtils.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gflags/gflags.h"
#include "grpc/support/log.h"
#include "grpc/support/time.h"
#include "grpcpp/get_clock.h"
#include "netinet/tcp.h"
#include "proc_parser.h"
#include "src/core/lib/rdma/RDMASenderReceiver.h"

class RDMAClient {
 public:
  RDMAClient(const BenchmarkConfig& config, const CommSpec& comm_spec)
      : config_(config), comm_spec_(comm_spec) {
    sockfd_ = SocketUtils::socket(AF_INET, SOCK_STREAM, 0);
    gethostname(hostname_, 255);
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
    if (config_.mpi_server && comm_spec_.worker_id() == 1 ||
        !config_.mpi_server && comm_spec_.worker_id() == 0) {
      gpr_log(GPR_INFO, "connect with %s", server_address);
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
    int rank = comm_spec_.worker_id();
    write_data(sockfd_, reinterpret_cast<char*>(&rank), sizeof(rank));
  }

  void set_should_work() {
    char should_work[1];

    memset(should_work, 0, 1);

    GPR_ASSERT(read_data(sockfd_, should_work, 1));
    config_.should_work = should_work[0] == '1';
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
    set_should_work();

    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }

    if (config_.affinity) {
      int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
      bind_thread_to_core(comm_spec_.local_id() % num_cores);
    }

    auto batch_size = config_.n_batch;
    cycles_t iter_cycles = 0;
    size_t n_iter = 0;

    auto send_msg = [&] {
      while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
      }
    };

    MPI_Barrier(comm_spec_.client_comm());
    auto cpu_time1 = get_cpu_time_per_core();
    auto before_tasklet = get_tasklet_count();
    auto t_begin = absl::Now();

    switch (config_.dir) {
      case Dir::kC2S: {
        for (int i = 0; i < batch_size; ++i) {
          cycles_t c1 = get_cycles();
          if (i == batch_size - 1) {  // notify server to exit
            data_out = -1;
          }
          send_msg();
          cycles_t c2 = get_cycles();
          iter_cycles += c2 - c1;
          if (config_.send_interval_us > 0) {
            gpr_sleep_until(gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(config_.send_interval_us, GPR_TIMESPAN)));
          }
        }
        n_iter = batch_size;
        break;
      }
      case Dir::kS2C: {
        bool running = true;
        cycles_t c1 = get_cycles();

        while (running) {
          while (!rdmasr.check_incoming()) {
          }

          size_t mlens = rdmasr.check_and_ack_incomings_locked();
          while (mlens > 0) {
            mlens -= rdmasr.recv(&msghdr_in);
            // Server notifies client to exit
            if (data_in == -1) {
              running = false;
              GPR_ASSERT(mlens == 0);
            }
          }
          n_iter++;
        }
        cycles_t c2 = get_cycles();
        iter_cycles += c2 - c1;
        break;
      }
      case Dir::kBi: {
        while (n_iter < batch_size && config_.should_work) {
          cycles_t c1 = get_cycles();
          send_msg();
          while (!rdmasr.check_incoming()) {
          }

          size_t mlens = rdmasr.check_and_ack_incomings_locked();
          mlens -= rdmasr.recv(&msghdr_in);
          GPR_ASSERT(mlens == 0);

          cycles_t c2 = get_cycles();
          iter_cycles += c2 - c1;
          n_iter++;
          if (config_.send_interval_us > 0) {
            gpr_sleep_until(gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(config_.send_interval_us, GPR_TIMESPAN)));
          }
        }
        // Notify server to exit
        data_out = -1;
        send_msg();
        break;
      }
    }

    auto t_end = absl::Now();
    auto cpu_time2 = get_cpu_time_per_core();
    auto after_tasklet = get_tasklet_count();
#if 0
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    uint64_t n_total_tasklet = 0;

    for (int i = 0; i < num_cores; i++) {
      n_total_tasklet += after_tasklet[i] - before_tasklet[i];
    }
    auto time_diff = cpu_time2["cpu"] - cpu_time1["cpu"];
    gpr_log(GPR_INFO,
            "[C%d-%s] CPU-TIME (s) user: %lf idle: %lf nice: %lf sys: %lf "
            "iowait: %lf "
            "irq: %lf softirq: %lf sum: %lf tasklet: %lu",
            comm_spec_.worker_id(), hostname_, time_diff.t_user,
            time_diff.t_idle, time_diff.t_nice, time_diff.t_system,
            time_diff.t_iowait, time_diff.t_irq, time_diff.t_softirq,
            time_diff.t_sum, n_total_tasklet);

    for (int i = 0; i < num_cores; i++) {
      auto cpu_name = "cpu" + std::to_string(i);
      time_diff = cpu_time2[cpu_name] - cpu_time1[cpu_name];
      auto n_tasklet = after_tasklet[i] - before_tasklet[i];
      gpr_log(GPR_INFO,
              "[C%d-%s] CPU%d-TIME (s) user: %lf idle: %lf nice: %lf sys: %lf "
              "iowait: %lf "
              "irq: %lf softirq: %lf sum: %lf tasklet: %lu",
              comm_spec_.worker_id(), hostname_, i, time_diff.t_user,
              time_diff.t_idle, time_diff.t_nice, time_diff.t_system,
              time_diff.t_iowait, time_diff.t_irq, time_diff.t_softirq,
              time_diff.t_sum, n_tasklet);
    }
#endif
    rusage rusage;
    getrusage(RUSAGE_THREAD, &rusage);
    int no_cpu_freq_fail = 0;
    double mhz = get_cpu_mhz(no_cpu_freq_fail);

    std::stringstream ss;
    ss << "Rank: " << comm_spec_.worker_id()
       << " Runtime: " << ToDoubleSeconds((t_end - t_begin)) << " s"
       << " Iter: " << n_iter << " Avg Iter: " << iter_cycles / mhz / n_iter
       << " us";
    ss << " nvcsw: " << rusage.ru_nvcsw << " nivscw: " << rusage.ru_nivcsw;
    gpr_log(GPR_INFO, "%s", ss.str().c_str());

    clientBarrier();
  }

  void RunEpoll() {
    RDMASenderReceiverEvent rdmasr;
    rdmasr.connect(sockfd_);
    rdmasr.WaitConnect();
    set_should_work();
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

    auto batch_size = config_.n_batch;
    epoll_event events[MAX_EVENTS];
    int failed_cnt = 0;

    auto send_msg = [&] {
      while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
        rdmasr.check_metadata();
        rdmasr.check_and_ack_incomings_locked();
        failed_cnt++;
      }
    };

    cycles_t iter_cycles = 0;
    cycles_t epoll_cycles = 0;
    size_t n_iter = 0;
    size_t epoll_count = 0;

    MPI_Barrier(comm_spec_.client_comm());

    auto cpu_time1 = get_cpu_time_per_core();
    auto before_tasklet = get_tasklet_count();
    auto t_begin = absl::Now();

    switch (config_.dir) {
      case Dir::kC2S: {
        for (int i = 0; i < batch_size; ++i) {
          cycles_t c1 = get_cycles();
          if (i == batch_size - 1) {  // notify server to exit
            data_out = -1;
          }
          send_msg();
          cycles_t c2 = get_cycles();
          iter_cycles += c2 - c1;
          if (config_.send_interval_us > 0) {
            gpr_sleep_until(gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(config_.send_interval_us, GPR_TIMESPAN)));
          }
        }
        n_iter = batch_size;
        break;
      }
      case Dir::kS2C: {
        bool running = true;
        auto process_event = [&](RDMASenderReceiverEvent* rdmasr) {
          auto rest_mlen = rdmasr->check_and_ack_incomings_locked();

          while (rest_mlen > 0) {
            rest_mlen -= rdmasr->recv(&msghdr_in);

            if (data_in == -1) {
              running = false;
              GPR_ASSERT(rest_mlen == 0);
            }
          }
        };

        cycles_t c1 = get_cycles();
        while (running) {
          int r;

          do {
            r = epoll_wait(epfd, events, MAX_EVENTS, config_.client_timeout);
          } while ((r < 0 && errno == EINTR) || r == 0);

          for (int j = 0; j < r; j++) {
            epoll_event& ev = events[j];
            void* data_ptr = ev.data.ptr;

            if ((reinterpret_cast<intptr_t>(data_ptr) & 1) == 1) {
              auto* rdmasr_ev = reinterpret_cast<RDMASenderReceiverEvent*>(
                  ~static_cast<intptr_t>(1) &
                  reinterpret_cast<intptr_t>(ev.data.ptr));

              if ((ev.events & EPOLLIN) != 0) {
                rdmasr_ev->check_metadata();
                process_event(rdmasr_ev);
              }
            } else if ((reinterpret_cast<intptr_t>(data_ptr) & 2) == 2) {
              auto* rdmasr_ev = reinterpret_cast<RDMASenderReceiverEvent*>(
                  ~static_cast<intptr_t>(2) &
                  reinterpret_cast<intptr_t>(ev.data.ptr));
              if ((ev.events & EPOLLIN) != 0) {
                rdmasr_ev->check_data();
                process_event(rdmasr_ev);
              }
            }
          }
        }
        cycles_t c2 = get_cycles();
        iter_cycles += c2 - c1;
        n_iter++;
        break;
      }
      case Dir::kBi: {
        auto process_event = [&](RDMASenderReceiverEvent* rdmasr) {
          auto rest_mlen = rdmasr->check_and_ack_incomings_locked();

          if (rest_mlen > 0) {
            rest_mlen -= rdmasr->recv(&msghdr_in);
            GPR_ASSERT(rest_mlen == 0);
            send_msg();
          }
        };

        send_msg();

        while (n_iter < batch_size && config_.should_work) {
          cycles_t c1 = get_cycles();
          int r;
          do {
            r = epoll_wait(epfd, events, MAX_EVENTS, config_.client_timeout);
          } while ((r < 0 && errno == EINTR) || r == 0);
          cycles_t c2 = get_cycles();
          epoll_cycles += c2 - c1;
          epoll_count++;

          for (int j = 0; j < r; j++) {
            epoll_event& ev = events[j];
            void* data_ptr = ev.data.ptr;

            if ((reinterpret_cast<intptr_t>(data_ptr) & 1) == 1) {
              auto* rdmasr_ev = reinterpret_cast<RDMASenderReceiverEvent*>(
                  ~static_cast<intptr_t>(1) &
                  reinterpret_cast<intptr_t>(ev.data.ptr));

              if ((ev.events & EPOLLIN) != 0) {
                rdmasr_ev->check_metadata();
                process_event(rdmasr_ev);
              }
            } else if ((reinterpret_cast<intptr_t>(data_ptr) & 2) == 2) {
              auto* rdmasr_ev = reinterpret_cast<RDMASenderReceiverEvent*>(
                  ~static_cast<intptr_t>(2) &
                  reinterpret_cast<intptr_t>(ev.data.ptr));

              if ((ev.events & EPOLLIN) != 0) {
                rdmasr_ev->check_data();
                process_event(rdmasr_ev);
              }
            }
          }

          cycles_t c3 = get_cycles();
          iter_cycles += c3 - c1;
          n_iter++;
          if (config_.send_interval_us > 0) {
            gpr_sleep_until(gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(config_.send_interval_us, GPR_TIMESPAN)));
          }
        }
        // Notify server to exit
        data_out = -1;
        send_msg();
        break;
      }
    }

    auto t_end = absl::Now();
    auto cpu_time2 = get_cpu_time_per_core();
    auto after_tasklet = get_tasklet_count();
#if 0
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    uint64_t n_total_tasklet = 0;

    for (int i = 0; i < num_cores; i++) {
      n_total_tasklet += after_tasklet[i] - before_tasklet[i];
    }
    auto time_diff = cpu_time2["cpu"] - cpu_time1["cpu"];

    gpr_log(GPR_INFO,
            "[C%d-%s] CPU-TIME (s) user: %lf idle: %lf nice: %lf sys: %lf "
            "iowait: %lf "
            "irq: %lf softirq: %lf sum: %lf tasklet: %lu",
            comm_spec_.worker_id(), hostname_, time_diff.t_user,
            time_diff.t_idle, time_diff.t_nice, time_diff.t_system,
            time_diff.t_iowait, time_diff.t_irq, time_diff.t_softirq,
            time_diff.t_sum, n_total_tasklet);

    for (int i = 0; i < num_cores; i++) {
      auto cpu_name = "cpu" + std::to_string(i);
      time_diff = cpu_time2[cpu_name] - cpu_time1[cpu_name];
      auto n_tasklet = after_tasklet[i] - before_tasklet[i];
      gpr_log(GPR_INFO,
              "[C%d-%s] CPU%d-TIME (s) user: %lf idle: %lf nice: %lf sys: %lf "
              "iowait: %lf "
              "irq: %lf softirq: %lf sum: %lf tasklet: %lu",
              comm_spec_.worker_id(), hostname_, i, time_diff.t_user,
              time_diff.t_idle, time_diff.t_nice, time_diff.t_system,
              time_diff.t_iowait, time_diff.t_irq, time_diff.t_softirq,
              time_diff.t_sum, n_tasklet);
    }
#endif
    rusage rusage;
    getrusage(RUSAGE_THREAD, &rusage);
    int no_cpu_freq_fail = 0;
    double mhz = get_cpu_mhz(no_cpu_freq_fail);

    std::stringstream ss;
    ss << "Rank: " << comm_spec_.worker_id()
       << " Runtime: " << ToDoubleSeconds((t_end - t_begin)) << " s"
       << " Iter: " << n_iter << " Avg Iter: " << iter_cycles / mhz / n_iter
       << " us";
    if (epoll_count > 0) {
      ss << " Epoll Lat: " << epoll_cycles / mhz / epoll_count << " us";
    }
    ss << " nvcsw: " << rusage.ru_nvcsw << " nivscw: " << rusage.ru_nivcsw;
    gpr_log(GPR_INFO, "%s", ss.str().c_str());
  }

  void RunBPEV() {
    RDMASenderReceiverBPEV rdmasr;
    rdmasr.connect(sockfd_);
    rdmasr.WaitConnect();
    set_should_work();
    // Waiting for all clients connected
    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }

    if (config_.affinity) {
      int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
      bind_thread_to_core(comm_spec_.local_id() % num_cores);
    }

    int epfd = epoll_create1(EPOLL_CLOEXEC);

    int wakeup_fd = rdmasr.get_wakeup_fd();
    struct epoll_event wakeup_ep_ev;
    wakeup_ep_ev.events =
        static_cast<uint32_t>(EPOLLIN | EPOLLET | EPOLLEXCLUSIVE);
    wakeup_ep_ev.data.ptr =
        reinterpret_cast<void*>(reinterpret_cast<intptr_t>(&rdmasr) | 1);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, wakeup_fd, &wakeup_ep_ev) != 0) {
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
    int send_idx = 0;

    auto batch_size = config_.n_batch;
    epoll_event events[MAX_EVENTS];

    auto send_msg = [&] {
      if (data_out != -1) {
        data_out = send_idx++;
      }
      while (!rdmasr.send(&msghdr_out, sizeof(data_out))) {
        gpr_log(GPR_ERROR, "Failed to send");
      }
      //      gpr_log(GPR_INFO, "Sent %lu", data_out);
    };

    cycles_t iter_cycles = 0;
    size_t n_iter = 0;

    MPI_Barrier(comm_spec_.client_comm());

    auto t_begin = absl::Now();

    switch (config_.dir) {
      case Dir::kC2S: {
        for (int i = 0; i < batch_size; ++i) {
          cycles_t c1 = get_cycles();
          if (i == batch_size - 1) {  // notify server to exit
            gpr_log(GPR_INFO, "Notify to exit");
            data_out = -1;
          }
          send_msg();
          cycles_t c2 = get_cycles();
          iter_cycles += c2 - c1;
          if (config_.send_interval_us > 0) {
            gpr_sleep_until(gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(config_.send_interval_us, GPR_TIMESPAN)));
          }
        }
        n_iter = batch_size;
        break;
      }
      case Dir::kS2C: {
        bool running = true;
        auto process_event = [&](RDMASenderReceiverBPEV* rdmasr) {
          auto rest_mlen = rdmasr->check_and_ack_incomings_locked();

          while (rest_mlen > 0) {
            rest_mlen -= rdmasr->recv(&msghdr_in);

            if (data_in == -1) {
              running = false;
              GPR_ASSERT(rest_mlen == 0);
            }
          }
        };

        cycles_t c1 = get_cycles();
        while (running) {
          int r;

          do {
            r = epoll_wait(epfd, events, MAX_EVENTS, config_.client_timeout);
          } while ((r < 0 && errno == EINTR) || r == 0);

          for (int j = 0; j < r; j++) {
            epoll_event& ev = events[j];
            void* data_ptr = ev.data.ptr;

            if ((reinterpret_cast<intptr_t>(data_ptr) & 1) == 1) {
              auto* rdmasr_ev = reinterpret_cast<RDMASenderReceiverBPEV*>(
                  ~static_cast<intptr_t>(1) &
                  reinterpret_cast<intptr_t>(ev.data.ptr));
              if ((ev.events & EPOLLIN) != 0) {
                ssize_t sz;
                uint64_t val;
                do {
                  sz = read(rdmasr_ev->get_wakeup_fd(), &val, sizeof(val));
                } while (sz < 0 && errno == EAGAIN);
                process_event(rdmasr_ev);
              }
            }
          }
        }
        cycles_t c2 = get_cycles();
        iter_cycles += c2 - c1;
        n_iter++;
        break;
      }
      case Dir::kBi: {
        auto process_event = [&](RDMASenderReceiverBPEV* rdmasr) {
          auto rest_mlen = rdmasr->check_and_ack_incomings_locked();

          if (rest_mlen > 0) {
            rest_mlen -= rdmasr->recv(&msghdr_in);
            GPR_ASSERT(rest_mlen == 0);
            send_msg();
            //            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        };

        send_msg();

        while (n_iter < batch_size && config_.should_work) {
          cycles_t c1 = get_cycles();
          int r;
          do {
            r = epoll_wait(epfd, events, MAX_EVENTS, config_.client_timeout);
          } while ((r < 0 && errno == EINTR) || r == 0);

          for (int j = 0; j < r; j++) {
            epoll_event& ev = events[j];
            void* data_ptr = ev.data.ptr;

            if ((reinterpret_cast<intptr_t>(data_ptr) & 1) == 1) {
              auto* rdmasr_ev = reinterpret_cast<RDMASenderReceiverBPEV*>(
                  ~static_cast<intptr_t>(1) &
                  reinterpret_cast<intptr_t>(ev.data.ptr));

              if ((ev.events & EPOLLIN) != 0) {
                ssize_t sz;
                uint64_t val;
                do {
                  sz = read(rdmasr_ev->get_wakeup_fd(), &val, sizeof(val));
                } while (sz < 0 && errno == EAGAIN);
                process_event(rdmasr_ev);
              }
            }
          }
          cycles_t c2 = get_cycles();
          iter_cycles += c2 - c1;
          n_iter++;
          if (config_.send_interval_us > 0) {
            gpr_sleep_until(gpr_time_add(
                gpr_now(GPR_CLOCK_REALTIME),
                gpr_time_from_micros(config_.send_interval_us, GPR_TIMESPAN)));
          }
        }
        // Notify server to exit
        data_out = -1;
        send_msg();
        break;
      }
    }

    auto t_end = absl::Now();

    rusage rusage;
    getrusage(RUSAGE_THREAD, &rusage);
    int no_cpu_freq_fail = 0;
    double mhz = get_cpu_mhz(no_cpu_freq_fail);

    std::stringstream ss;
    ss << "Rank: " << comm_spec_.worker_id()
       << " Runtime: " << ToDoubleSeconds((t_end - t_begin)) << " s"
       << " Iter: " << n_iter << " Avg Iter: " << iter_cycles / mhz / n_iter
       << " us";
    ss << " nvcsw: " << rusage.ru_nvcsw << " nivscw: " << rusage.ru_nivcsw;
    gpr_log(GPR_INFO, "%s", ss.str().c_str());

    clientBarrier();
  }

  void RunTCP() {
    // Waiting for all clients connected
    if (config_.mpi_server) {
      MPI_Barrier(comm_spec_.comm());
    }

    if (config_.affinity) {
      int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
      bind_thread_to_core(comm_spec_.local_id() % num_cores);
    }

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ep_event;
    ep_event.events =
        static_cast<uint32_t>(EPOLLIN | EPOLLOUT | EPOLLEXCLUSIVE);
    ep_event.data.fd = sockfd_;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd_, &ep_event) != 0) {
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

    memset(&msghdr_in, 0, sizeof(msghdr));
    memset(&msghdr_out, 0, sizeof(msghdr));

    iov_out.iov_base = &data_out;
    iov_out.iov_len = sizeof(data_out);
    iov_in.iov_base = &data_in;
    iov_in.iov_len = sizeof(data_in);
    msghdr_out.msg_iov = &iov_out;
    msghdr_out.msg_iovlen = 1;
    msghdr_in.msg_iov = &iov_in;
    msghdr_in.msg_iovlen = 1;

    absl::Time t_begin;
    auto batch_size = config_.n_batch;
    epoll_event events[MAX_EVENTS];
    int failed_cnt = 0;
    size_t send_ok_count = 0, recv_ok_count = 0;
    size_t epoll_count = 0, send_count = 0, recv_count = 0;
    cycles_t epoll_cycles = 0, send_cycles = 0, recv_cycles = 0;
    std::queue<int64_t> failed_data;

    auto send_msg = [&] {
      auto c1 = get_cycles();
      auto byte_sent = tcp_send1(sockfd_, &msghdr_out);
      auto c2 = get_cycles();

      send_cycles += c2 - c1;
      send_count++;

      if (byte_sent == -1) {
        failed_data.emplace(data_out);
        return false;
      }

      send_ok_count++;
      GPR_ASSERT(byte_sent == sizeof(data_out));
      return true;
    };

    auto process_event = [&](int fd) {
      bool c_should_exit = false;
      ssize_t read_bytes;

      do {
        auto c1 = get_cycles();
        read_bytes = recvmsg(fd, &msghdr_in, 0);
        auto c2 = get_cycles();
        recv_cycles += c2 - c1;
        recv_count++;
      } while (read_bytes < 0 && errno == EINTR);

      if (read_bytes == -1) {
        return c_should_exit;
      }

      recv_ok_count++;

      if (config_.dir == Dir::kBi && recv_ok_count == batch_size ||
          config_.dir == Dir::kS2C && data_in == -1) {
        c_should_exit = true;
      }

      if (config_.dir == Dir::kBi) {
        send_msg();
      }

      return c_should_exit;
    };

    MPI_Barrier(comm_spec_.client_comm());

    t_begin = absl::Now();

    while (true) {
      switch (config_.dir) {
        case Dir::kC2S: {
          send_msg();
          if (send_ok_count == batch_size) {
            goto finish;
          }
          break;
        }
        case Dir::kBi: {
          // send first msg
          if (send_ok_count == 0) {
            send_msg();
          }
          break;
        }
      }

      int r;
      cycles_t c1 = get_cycles();
      do {
        r = epoll_wait(epfd, events, MAX_EVENTS, config_.client_timeout);
      } while ((r < 0 && errno == EINTR) || r == 0);
      cycles_t c2 = get_cycles();
      epoll_cycles += c2 - c1;
      epoll_count++;

      for (int j = 0; j < r; j++) {
        epoll_event& ev = events[j];

        if ((ev.events & EPOLLIN) != 0) {
          if (process_event(ev.data.fd)) {
            goto finish;
          }
        }

        if ((ev.events & EPOLLOUT) != 0) {
          while (!failed_data.empty()) {
            data_out = failed_data.front();
            if (!send_msg()) {
              failed_data.pop();
              break;
            }
            failed_data.pop();
          }
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
    int no_cpu_freq_fail = 0;
    double mhz = get_cpu_mhz(no_cpu_freq_fail);

    std::stringstream ss;
    ss << "Rank: " << comm_spec_.worker_id();
    if (send_count > 0) {
      ss << " Send Latency: " << send_cycles / mhz / send_count;
    }
    if (recv_count > 0) {
      ss << " Recv Latency: " << recv_cycles / mhz / recv_count;
    }
    if (epoll_count > 0) {
      ss << " Epoll Time: " << epoll_cycles / mhz / epoll_count;
    }
    ss << " nvcsw: " << rusage.ru_nvcsw << " nivscw: " << rusage.ru_nivcsw;
    ss << " failed cnt: " << failed_cnt;
    gpr_log(GPR_INFO, "%s", ss.str().c_str());
    clientBarrier();
  }

 private:
  BenchmarkConfig config_;
  CommSpec comm_spec_;
  int sockfd_;
  char hostname_[255];

  void clientBarrier() {
    MPI_Request request;
    MPI_Ibarrier(comm_spec_.client_comm(), &request);

    int flag = 0;
    do {
      MPI_Test(&request, &flag, MPI_STATUS_IGNORE);
      std::this_thread::yield();
    } while (flag == 0);
  }
};

#endif  // MICROBENCHMARK_MB_CLIENT_H