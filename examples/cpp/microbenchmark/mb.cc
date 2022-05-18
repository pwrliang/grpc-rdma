#define SENDER_RECEIVER_NON_ATOMIC
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "comm_spec.h"
#include "flags.h"
#include "mb.h"
#include "mb_client.h"
#include "mb_server.h"

void StartServer(const BenchmarkConfig& config, const CommSpec& comm_spec) {
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
    config.n_client = FLAGS_nclient;
    config.n_active_client = FLAGS_max_worker;
    config.n_batch = FLAGS_batch;
    config.affinity = FLAGS_affinity;
    config.server_timeout = FLAGS_server_timeout;
    config.client_timeout = FLAGS_client_timeout;

    GPR_ASSERT(config.mpi_server && config.n_client == 0 ||
               !config.mpi_server && config.n_client > 0);
    GPR_ASSERT(config.n_batch > 0);
    GPR_ASSERT(config.n_max_polling_thread > 0);

    if (FLAGS_mpiserver) {
      if (comm_spec.worker_id() == 0) {
        if (comm_spec.local_num() > 1) {
          char hn[255];
          int hn_len;
          MPI_Get_processor_name(hn, &hn_len);
          gpr_log(GPR_ERROR,
                  "Trying to launch multiple server on the same node %s", hn);
          exit(1);
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