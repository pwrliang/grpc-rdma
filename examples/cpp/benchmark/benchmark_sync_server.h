#ifndef BENCHMARK_SYNC_SERVER
#define BENCHMARK_SYNC_SERVER

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
#include <grpcpp/impl/channel_argument_option.h>
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


class BenchmarkSyncService final : public BENCHMARK::Service {
  public:
    Status SayHello(ServerContext* context, const Data_Empty* request, Data_Empty* reply) override;
    Status Unary(ServerContext* context, const Complex* request, Complex* reply) override;
    Status ClientStream(ServerContext* context, ServerReader<Complex>* reader, Complex* reply) override;
    Status ServerStream(ServerContext* context, const Complex* request, ServerWriter<Complex>* writer) override;
    Status BiStream(ServerContext* context, ServerReaderWriter<Complex, Complex>* stream) override;
  private:
    std::mutex mu_;
    // TimerPackage timer_;
};

class BenchmarkSyncServer {
  public:
    BenchmarkSyncServer(const std::string server_address);

    void Run();

  private:
    std::unique_ptr<BenchmarkSyncService> sync_service_;
    std::unique_ptr<ServerBuilder> builder_;
    std::unique_ptr<grpc::Server> server_;
};


// -----< BenchmarkSyncServer >-----
BenchmarkSyncServer::BenchmarkSyncServer(const std::string server_address)
  : builder_(new ServerBuilder()), sync_service_(new BenchmarkSyncService()) {
  // builder_->SetOption(grpc::MakeChannelArgumentOption(GRPC_ARG_ALLOW_REUSEPORT, 0));
  builder_->AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder_->RegisterService(sync_service_.get());
  server_ = builder_->BuildAndStart();
}

void BenchmarkSyncServer::Run() {
  server_->Wait();
}



// -----< BenchmarkSyncService >-----
Status BenchmarkSyncService::SayHello(ServerContext* context, const Data_Empty* request,
                              Data_Empty* reply) {
  return Status::OK;
}

Status BenchmarkSyncService::Unary(ServerContext* context, const Complex* request,
                           Complex* reply) {
  reply->mutable_datas()->mutable_data1()->resize(
      request->numbers().number1());
  return Status::OK;
}

Status BenchmarkSyncService::ClientStream(ServerContext* context, ServerReader<Complex>* reader,
                                  Complex* reply) {
  Complex request;
  size_t total_data_size = 0;
  for (size_t id = 0; reader->Read(&request); id++) {
    total_data_size += request.datas().data1().length();
  }
  reply->mutable_numbers()->set_number1(total_data_size);
  return Status::OK;
}

Status BenchmarkSyncService::ServerStream(ServerContext* context, const Complex* request,
                                  ServerWriter<Complex>* writer) {
  size_t batch_size = request->numbers().number1();
  size_t min_request_size = request->numbers().number2();
  size_t max_request_size = request->numbers().number3();
  size_t request_size;
  Complex reply;
  for (size_t i = 0; i < batch_size; i++) {
    request_size = random(min_request_size, max_request_size);
    reply.mutable_datas()->mutable_data1()->resize(request_size);
    reply.mutable_numbers()->set_number1(min_request_size);
    reply.mutable_numbers()->set_number2(max_request_size);
    writer->Write(reply);
  }
  return Status::OK;
}

Status BenchmarkSyncService::BiStream(ServerContext* context,
                              ServerReaderWriter<Complex, Complex>* stream) {
  Complex request, reply;
  for (size_t id = 0; stream->Read(&request); id++) {
    // std::unique_lock<std::mutex> lock(mu_);
    reply.mutable_numbers()->set_number1(request.datas().data1().length());
    reply.mutable_datas()->mutable_data1()->resize(
        request.numbers().number1());
    stream->Write(reply);
  }
  return Status::OK;
}

#endif // #ifndef BENCHMARK_SYNC_SERVER