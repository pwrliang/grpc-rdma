#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "comm_spec.h"
#include "gflags/gflags.h"
#include "mb.h"
#include "mb_client.h"
#include "mb_server.h"

void StartServer(Mode mode, const CommSpec& comm_spec) {
  RDMAServer server(mode, comm_spec);
  int fd;
  ifreq ifr;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, "ib0", IFNAMSIZ - 1);
  ioctl(fd, SIOCGIFADDR, &ifr);
  close(fd);

  char* ip =
      inet_ntoa((reinterpret_cast<sockaddr_in*>(&ifr.ifr_addr))->sin_addr);
  MPI_Bcast(ip, 16, MPI_CHAR, 0, comm_spec.comm());
  server.start(FLAGS_port);
}

void StartClient(Mode mode, const CommSpec& comm_spec) {
  RDMAClient client(comm_spec);

  char ip[16];
  MPI_Bcast(ip, 16, MPI_CHAR, 0, comm_spec.comm());

  client.connect(ip, FLAGS_port);
  if (mode == Mode::kBusyPolling || mode == Mode::kBusyPollingRR) {
    client.RunBusyPolling();
  } else if (mode == Mode::kEvent) {
    client.RunEpoll();
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
    Mode mode = parse_mode(FLAGS_mode);

    if (comm_spec.worker_id() == 0) {
      GPR_ASSERT(comm_spec.local_num() == 1);
      StartServer(mode, comm_spec);
    } else {
      StartClient(mode, comm_spec);
    }
  }
  gflags::ShutDownCommandLineFlags();
  FinalizeMPIComm();
  return 0;
}