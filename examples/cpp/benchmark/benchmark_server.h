#ifndef BENCHMARK_SERVER
#define BENCHMARK_SERVER

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include "benchmark_sync_server.h"
#include "benchmark_async_server.h"
#include "benchmark_utils.h"

#ifdef BAZEL_BUILD
#include "examples/protos/benchamrk.grpc.pb.h"
#else

#include "benchmark.grpc.pb.h"

#endif

using benchmark::BENCHMARK;
using benchmark::Complex;
using benchmark::Data_Bytes;
using benchmark::Data_Empty;
using benchmark::Data_Int64;
using benchmark::Data_String;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerAsyncReader;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerAsyncWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

class BenchmarkServer {
  public:
    BenchmarkServer(const std::string server_address, bool async_enable);
    
    void Run(const size_t thread_num, const size_t cq_num);

  private:
    void SyncServicesRun();
    void AsyncServicesRun(const size_t thread_num, const size_t cq_num);

    bool async_enable_;
    std::unique_ptr<BenchmarkSyncServer> sync_server_;
    std::unique_ptr<BenchmarkAsyncServer> async_server_;
};



// -----< BenchmarkServer >-----
BenchmarkServer::BenchmarkServer(const std::string server_address, bool async_enable)
  : async_enable_(async_enable) {
  if (async_enable) {
    async_server_ = unique_ptr<BenchmarkAsyncServer>(new BenchmarkAsyncServer(server_address));
    printf("Server works in Async mode\n");
  } else {
    sync_server_ = unique_ptr<BenchmarkSyncServer>(new BenchmarkSyncServer(server_address));
    printf("Server works in Sync mode\n");
  }
  printf("Server is listening on %s\n", server_address.c_str());
}

void BenchmarkServer::Run(const size_t thread_num, const size_t cq_num) {
  if (async_enable_) AsyncServicesRun(thread_num, cq_num);
  else SyncServicesRun();
}

void BenchmarkServer::SyncServicesRun() {
  if (async_enable_) return;
  sync_server_->Run();
}

void BenchmarkServer::AsyncServicesRun(const size_t thread_num, const size_t cq_num) {
  if (!async_enable_) return;
  async_server_->Run(thread_num, cq_num);

}


#endif // #ifndef BENCHMARK_SERVER















