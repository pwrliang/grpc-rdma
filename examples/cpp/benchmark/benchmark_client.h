#ifndef BENCHMARK_CLIENT
#define BENCHMARK_CLIENT

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include "benchmark_sync_client.h"
#include "benchmark_async_client.h"
#include "benchmark_utils.h"

#ifdef BAZEL_BUILD
#include "examples/protos/benchmark.grpc.pb.h"
#else

#include "benchmark.grpc.pb.h"

#endif

using benchmark::BENCHMARK;
using benchmark::Complex;
using benchmark::Data_Bytes;
using benchmark::Data_Empty;
using benchmark::Data_Int64;
using benchmark::Data_String;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::CompletionQueue;
using grpc::ClientAsyncResponseReader;
using grpc::ClientAsyncWriter;
using grpc::Status;

void MPI_summary_time(int64_t time, const char* prefix, const char* unit);
void MPI_Summary_throughput(double tpt, const char* prefix, const char* unit);

class BenchmarkClient {
  public:
    BenchmarkClient(std::shared_ptr<Channel> channel)
      : sync_client_(new BenchmarkSyncClient(channel)),
        async_client_(new BenchmarkAsyncClient(channel)) {}

    void SyncSayhello() {sync_client_->SyncSayHello(); }

    void SyncOperations(const size_t batch_size, const size_t data_size);

    void AsyncOperations(const size_t batch_size, const size_t data_size);

  private:
    std::unique_ptr<BenchmarkSyncClient> sync_client_;
    std::unique_ptr<BenchmarkAsyncClient> async_client_;
};



// -----< BenchmarkClient >-----
void BenchmarkClient::AsyncOperations(const size_t batch_size, const size_t data_size) {
  CompletionQueue cq;
  std::thread worker(async_client_->NextAndProcees, &cq);

  async_client_->AsyncSayHelloStart(&cq);
  async_client_->AsyncUnaryStart(&cq, batch_size, data_size / 2, data_size * 2);
  async_client_->AsyncClientStreamStart(&cq, batch_size, data_size);
  printf("rank %d: batch size = %lld, data size = %lld, all async operations started\n", world_rank, batch_size, data_size);

  worker.join();
  printf("rank %d: batch size = %lld, data size = %lld, all async operations finished\n", world_rank, batch_size, data_size);
}




void BenchmarkClient::SyncOperations(const size_t batch_size, const size_t data_size) {
  std::stringstream ss;
  size_t us;

  sync_client_->SyncSayHello();

  auto t0 = std::chrono::high_resolution_clock::now();
  sync_client_->SyncUnary(batch_size, data_size / 2, data_size * 2);
  auto t1 = std::chrono::high_resolution_clock::now();
  us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  ss.str("");
  ss << "SyncUnary, batch size = " << batch_size
      << ", data size = " << data_size;
  MPI_summary_time(us, ss.str().c_str(), "us");
  MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");

  auto t2 = std::chrono::high_resolution_clock::now();
  sync_client_->SyncClientStream(batch_size, data_size);
  auto t3 = std::chrono::high_resolution_clock::now();
  us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
  ss.str("");
  ss << "SyncClientStream, batch size = " << batch_size
      << ", data size = " << data_size;
  MPI_summary_time(us, ss.str().c_str(), "us");
  MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");

  auto t4 = std::chrono::high_resolution_clock::now();
  sync_client_->SyncServerStream(batch_size, data_size);
  auto t5 = std::chrono::high_resolution_clock::now();
  us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
  ss.str("");
  ss << "SyncServerStream, batch size = " << batch_size
      << ", data size = " << data_size;
  MPI_summary_time(us, ss.str().c_str(), "us");
  MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");

  auto t6 = std::chrono::high_resolution_clock::now();
  sync_client_->SyncBiStream(batch_size, data_size / 2, data_size * 2);
  auto t7 = std::chrono::high_resolution_clock::now();
  us = std::chrono::duration_cast<std::chrono::microseconds>(t7 - t6).count();
  ss.str("");
  ss << "SyncBiStream, batch size = " << batch_size
      << ", data size = " << data_size;
  MPI_summary_time(us, ss.str().c_str(), "us");
  MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");
}


#endif // #ifndef BENCHMARK_CLIENT