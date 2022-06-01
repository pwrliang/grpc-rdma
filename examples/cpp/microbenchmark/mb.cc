#define SENDER_RECEIVER_NON_ATOMIC
#include "mb.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "comm_spec.h"
#include "flags.h"
#include "mb_client.h"
#include "mb_server.h"
#include "proc_parser.h"

extern bool rdmasr_is_server;
void StartServer(const BenchmarkConfig& config, const CommSpec& comm_spec) {
  rdmasr_is_server = true;
  RDMAServer server(config, comm_spec);
  int fd;
  ifreq ifr;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, "ib0", IFNAMSIZ - 1);
  ioctl(fd, SIOCGIFADDR, &ifr);
  close(fd);
  if (config.mpi_server) {
    char* ip =
        inet_ntoa((reinterpret_cast<sockaddr_in*>(&ifr.ifr_addr))->sin_addr);
    MPI_Bcast(ip, 16, MPI_CHAR, 0, comm_spec.comm());
  }
  server.start(FLAGS_port);
}

void StartClient(const BenchmarkConfig& config, const CommSpec& comm_spec) {
  rdmasr_is_server = false;
  RDMAClient client(config, comm_spec);

  if (config.mpi_server) {
    char ip[16];
    MPI_Bcast(ip, 16, MPI_CHAR, 0, comm_spec.comm());
    client.connect(ip, FLAGS_port);
  } else {
    client.connect(FLAGS_host.c_str(), FLAGS_port);
  }

  if (config.mode == Mode::kBusyPolling ||
      config.mode == Mode::kBusyPollingRR) {
    client.RunBusyPolling();
  } else if (config.mode == Mode::kEvent) {
    client.RunEpoll();
  } else if (config.mode == Mode::kBPEV) {
    client.RunBPEV();
  } else if (config.mode == Mode::kTCP) {
    client.RunTCP();
  }
}

int main(int argc, char* argv[]) {
  setenv("GRPC_VERBOSITY", "INFO", 1);
  gpr_log_verbosity_init();
  gflags::SetUsageMessage("Usage: mpiexec [mpi_opts] ./mb [mb_opts]");
  if (argc == 1) {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "mb");
    exit(1);
  }
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  InitMPIComm();
  {
    CommSpec comm_spec;
    comm_spec.Init(MPI_COMM_WORLD);
    BenchmarkConfig config;
    config.mode = parse_mode(FLAGS_mode);
    config.dir = parse_dir(FLAGS_dir);
    config.mpi_server = FLAGS_mpiserver;
    config.n_max_polling_thread = FLAGS_polling_thread;
    config.n_batch = FLAGS_batch;
    config.affinity = FLAGS_affinity;
    config.server_timeout = FLAGS_server_timeout;
    config.client_timeout = FLAGS_client_timeout;
    config.send_interval_us = FLAGS_send_interval;
    config.n_cpu_limit = FLAGS_cpu_limit;
    config.n_work_thread = FLAGS_work_thread;
    config.work_stealing = FLAGS_work_stealing;
    if (config.mpi_server) {
      config.n_client = comm_spec.worker_num() - 1;
    } else {
      config.n_client = FLAGS_nclient;
    }

    GPR_ASSERT(config.n_client > 0);
    GPR_ASSERT(config.n_batch > 0);
    GPR_ASSERT(config.n_max_polling_thread > 0);
    GPR_ASSERT(!config.affinity || config.affinity && config.n_cpu_limit == -1);
    
    if (config.mpi_server) {
      if (comm_spec.worker_id() == 0) {
        if (comm_spec.local_num() > 1) {
          char hn[255];
          int hn_len;
          MPI_Get_processor_name(hn, &hn_len);
          gpr_log(GPR_ERROR,
                  "Trying to launch server and client on the same node %s", hn);
          abort();
        }
        StartServer(config, comm_spec);
      } else {
        StartClient(config, comm_spec);
      }
    } else {
      if (FLAGS_host.empty()) {
        StartServer(config, comm_spec);
      } else {
        StartClient(config, comm_spec);
      }
    }
  }
  gflags::ShutDownCommandLineFlags();
  FinalizeMPIComm();
  return 0;
}