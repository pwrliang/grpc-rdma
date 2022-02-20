#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>

#ifdef BAZEL_BUILD
#include "examples/protos/benchamrk.grpc.pb.h"
#else
#include "benchmark.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using benchmark::BENCHMARK;
using benchmark::Data_Empty;
using benchmark::Data_Bytes;
using benchmark::Data_Int64;
using benchmark::Data_String;
using benchmark::Complex;

class SyncServeice final : public BENCHMARK::Service {
  public:

    Status SayHello(ServerContext* context, const Data_Empty* request, Data_Empty* reply) override {
      return Status::OK;
    }

    Status BiUnary(ServerContext* context, const Complex* request, Complex* reply) override {
      reply->mutable_datas()->mutable_data1()->resize(request->numbers().number1());
      // printf("%d, %d\n", reply->numbers().number1(), reply->mutable_datas()->mutable_data1()->length());
      return Status::OK;
    }

    // Status ClientStream(ServerContext* context, ServerReader<Data>* reader, Data* reply) override {
    //   Data_Bytes request;
    //   size_t data_size = 0;
    //   size_t batch_size = 0;
    //   while (reader->Read(&request)) {
    //     batch_size++;
    //     // data_size = request.data().length();
    //     if (data_size == 0) {
    //       data_size = request.data().length();
    //     } else if (data_size != request.ByteSizeLong()) {
    //       return Status::CANCELLED;
    //     }
    //   }
    //   reply->set_number1(batch_size);
    //   reply->set_number2(data_size);
    //   return Status::OK;
    // }
};

class BenchmarkServer {
  public:
    BenchmarkServer(const std::string server_address, 
                    bool sync_enable, 
                    bool async_enable, size_t async_threads_num, size_t async_cqs_num)
      : sync_enable_(sync_enable), 
        async_enable_(async_enable), async_threads_num_(async_threads_num), async_cqs_num_(async_cqs_num) {
      builder_.AddListeningPort(server_address, grpc::InsecureServerCredentials());
      if (sync_enable) {
        builder_.RegisterService(&sync_service_);
      }
      if (async_enable) {
        builder_.RegisterService(&async_service_);
        while (async_cqs_num--) {
          async_cqs_.push_back(builder_.AddCompletionQueue());
        }
      }
      server_ = builder_.BuildAndStart();
      printf("Server: sync enable %d; async enable %d, async threads num %d, async threads num %d\n",
             sync_enable, async_enable, async_threads_num, async_cqs_num);
      printf("Server is listening on %s\n", server_address.c_str());
    }

    void Run() {
      if (async_enable_) {

      }
      if (sync_enable_) {
        server_->Wait();
      }
    }

  private:
    ServerBuilder builder_;
    std::unique_ptr<grpc::Server> server_;

    SyncServeice sync_service_;
    bool sync_enable_ = false;

    BENCHMARK::AsyncService async_service_;
    bool async_enable_ = false;
    size_t async_threads_num_ = 0, async_cqs_num_ = 0;
    std::vector<std::thread> async_threads_;
    std::vector<std::unique_ptr<ServerCompletionQueue>> async_cqs_;
};

DEFINE_string(server_address, "localhost:50051", "");
DEFINE_bool(sync_enable, true, "");
DEFINE_bool(async_enable, false, "");
DEFINE_int32(async_cq_num, 1, "");
DEFINE_int32(async_thread_num, 1, "");
DEFINE_string(platform, "TCP", "which transport protocol used");
DEFINE_string(verbosity, "WARNING", "");

int main(int argc, char** argv) {
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  ::gflags::ShutDownCommandLineFlags();

  const std::string server_address = FLAGS_server_address;
  const bool sync_enable = FLAGS_sync_enable;
  const bool async_enable = FLAGS_async_enable;
  const int async_cq_num = FLAGS_async_cq_num;
  const int async_thread_num = FLAGS_async_thread_num;
  const std::string platform = FLAGS_platform;
  const std::string verbosity = FLAGS_verbosity;

  setenv("GRPC_PLATFORM_TYPE", platform.c_str(), 1);
  setenv("RDMA_VERBOSITY", verbosity.c_str(), 1);

  BenchmarkServer server(server_address, sync_enable, async_enable, async_cq_num, async_thread_num);

  server.Run();

  return 0;
}