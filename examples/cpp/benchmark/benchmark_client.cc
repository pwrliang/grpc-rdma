#include "benchmark_client.h"
#include <mpi.h>

void MPI_summary_time(int64_t time, const char* prefix, const char* unit) {
  // MPI_Barrier(MPI_COMM_WORLD);
  // sleep(1);
  if (_rdma_internal_world_rank_ != 0) {
    MPI_Send(&time, 1, MPI_INT64_T, 0, 0, MPI_COMM_WORLD);
    return;
  }
  int64_t total_time = time, _time_;
  for (int i = 1; i < _rdma_internal_world_size_; i++) {
    MPI_Recv(&_time_, 1, MPI_INT64_T, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    total_time += _time_;
  }
  printf(
      "%s: world size = %d, total time duration = %lld %s, average time "
      "duration = %lld %s\n",
      prefix, _rdma_internal_world_size_, total_time, unit, total_time / _rdma_internal_world_size_, unit);
}

void MPI_summary_throughput(double tpt, const char* prefix, const char* unit) {
  // MPI_Barrier(MPI_COMM_WORLD);
  // sleep(1);
  if (_rdma_internal_world_rank_ != 0) {
    MPI_Send(&tpt, 1, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD);
    return;
  }
  double total_tpt = tpt, _tpt_;
  for (int i = 1; i < _rdma_internal_world_size_; i++) {
    MPI_Recv(&_tpt_, 1, MPI_DOUBLE, i, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    total_tpt += _tpt_;
  }
  printf("%s, world size = %d, total throughput = %f %s, average throughput = %f %s\n",
          prefix, _rdma_internal_world_size_, total_tpt, unit, total_tpt / _rdma_internal_world_size_, unit);
}

void MPI_summary_cpu(double cpu, const char* prefix, const char* unit) {
  // MPI_Barrier(MPI_COMM_WORLD);
  // sleep(1);
  if (_rdma_internal_world_rank_ != 0) {
    MPI_Send(&cpu, 1, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD);
    return;
  }
  double total_cpu = cpu, _cpu_;
  for (int i = 1; i < _rdma_internal_world_size_; i++) {
    MPI_Recv(&_cpu_, 1, MPI_DOUBLE, i, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    total_cpu += _cpu_;
  }
  printf("%s, world size = %d, total cpu usage = %f %s, average cpu usage = %f %s\n",
          prefix, _rdma_internal_world_size_, total_cpu, unit, total_cpu / _rdma_internal_world_size_, unit);
}


DEFINE_string(server_address, "localhost:50051", "");
DEFINE_bool(sync_enable, true, "");
DEFINE_bool(async_enable, false, "");
DEFINE_string(platform, "TCP", "which transport protocol used");
DEFINE_string(verbosity, "ERROR", "");
// DEFINE_string(data_sizes, "64,1024,64*1024", "");
// DEFINE_string(batch_sizes, "5000,10000", "");
DEFINE_string(data_sizes, "1024*64", "");
DEFINE_string(batch_sizes, "5000", "");

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &_rdma_internal_world_size_);
  MPI_Comm_rank(MPI_COMM_WORLD, &_rdma_internal_world_rank_);
  // printf("world rank = %d, world size = %d\n", _rdma_internal_world_rank_, _rdma_internal_world_size_);
  

  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  ::gflags::ShutDownCommandLineFlags();

  const std::string server_address = FLAGS_server_address;
  const std::string platform = FLAGS_platform;
  const std::string verbosity = FLAGS_verbosity;
  const std::string _data_sizes_ = FLAGS_data_sizes;
  const std::string _batch_size_ = FLAGS_batch_sizes;

  std::vector<int> data_sizes = Split2Int(_data_sizes_, ',');
  std::vector<int> batch_sizes = Split2Int(_batch_size_, ',');

  setenv("GRPC_PLATFORM_TYPE", platform.c_str(), 1);
  setenv("RDMA_VERBOSITY", verbosity.c_str(), 1);

  {
    BenchmarkClient client(
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));
  
    // warmup 
    client.SyncSayhello();
    MPI_Barrier(MPI_COMM_WORLD);

    if (_rdma_internal_world_rank_ == 0) {
      printf("warm up finished\n");
    }

    for (int data_size: data_sizes) {
      for (int batch_size: batch_sizes) {
        MPI_Barrier(MPI_COMM_WORLD);
        // sleep(1);
        client.SyncOperations(batch_size, data_size);
        MPI_Barrier(MPI_COMM_WORLD);
        // sleep(1);
        client.AsyncOperations(batch_size, data_size);
      }
    }


    // sleep(2);
    // MPI_Barrier(MPI_COMM_WORLD);
  }

  sleep(1000);

  MPI_Finalize();
  return 0;
}