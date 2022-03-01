#include "benchmark_server.h"


DEFINE_string(server_address, "0.0.0.0:50051", "");
DEFINE_bool(async_enable, false, ""); // true for async, false for sync
DEFINE_int32(async_cq_num, 2, "");
DEFINE_int32(async_thread_num, 4, "");
DEFINE_string(platform, "TCP", "which transport protocol used");
DEFINE_string(verbosity, "ERROR", "");

int main(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  ::gflags::ShutDownCommandLineFlags();

  const std::string server_address = FLAGS_server_address;
  const bool async_enable = FLAGS_async_enable;
  const int async_cq_num = FLAGS_async_cq_num;
  const int async_thread_num = FLAGS_async_thread_num;
  const std::string platform = FLAGS_platform;
  const std::string verbosity = FLAGS_verbosity;

  setenv("GRPC_PLATFORM_TYPE", platform.c_str(), 1);
  setenv("RDMA_VERBOSITY", verbosity.c_str(), 1);

  BenchmarkServer server(server_address, async_enable, async_thread_num, async_cq_num);

  server.Run();

  return 0;
}