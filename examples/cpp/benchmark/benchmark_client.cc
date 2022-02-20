#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>

#ifdef BAZEL_BUILD
#include "examples/protos/benchmark.grpc.pb.h"
#else
#include "benchmark.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using benchmark::BENCHMARK;
using benchmark::Data_Empty;
using benchmark::Data_Bytes;
using benchmark::Data_String;
using benchmark::Data_Int64;
using benchmark::Complex;

int random(int min, int max) {
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}

class BenchmarkClient {
  public:
    BenchmarkClient(std::shared_ptr<Channel> channel) : stub_(BENCHMARK::NewStub(channel)) {}

    bool SyncSayHello() {
      ClientContext context;
      Data_Empty request, reply;
      Status status = stub_->SayHello(&context, request, &reply);
      return status.ok();
    }

    bool SyncBiUnary(size_t request_size, size_t reply_size) {
      Complex request, reply;
      ClientContext context;
      request.mutable_datas()->mutable_data1()->resize(request_size);
      request.mutable_numbers()->set_number1(reply_size);
      Status status = stub_->BiUnary(&context, request, &reply);
      if (!status.ok()) {
        printf("SyncBiUnary failed, status is not ok, request size = %d, reply size = %d\n", 
                request_size, reply_size);
        return false;
      }
      if (reply.datas().data1().length() != reply_size) {
        printf("SyncBiUnary failed, status is ok, request size = %d, reply size = %d, %d\n", 
                request_size, reply_size, reply.numbers().number1());
        return false;
      }
      // printf("succeed\n");
      return true;
    }

    // bool SyncClientStream(size_t data_size, size_t batch_size) {
    //   ClientContext context;
    //   Data_Bytes request;
    //   Data_Int64 reply;
    //   std::unique_ptr<ClientWriter<Data_Bytes>> writer(stub_->ClientStream(&context, &reply));
    //   request.mutable_data()->resize(data_size);
    //   while (batch_size--) {
    //     if (writer->Write(request)) {
    //       printf("ClientStream failed");
    //       exit(-1);
    //     }
    //   }
    //   writer->WritesDone();
    //   Status status = writer->Finish();
    //   if (status.ok() && reply.number1() == batch_size && reply.number2() == data_size) return true;
    //   return false;
    // }

    // bool SyncServerStream(size_t data_size, size_t batch_size) {

    // }

    void BatchOperations(const size_t batch_size, const size_t data_size) {
      int rest_batch_size = batch_size;
      while (rest_batch_size-- > 0) {
        if (!SyncBiUnary(random(data_size / 2, data_size * 2), random(data_size / 2, data_size * 2))) break;
      }
      if (rest_batch_size < 0) {
        printf("BatchOperations succeed, data size = %d, batch size = %d\n", data_size, batch_size);
      } else {
        printf("BatchOperations failed, data size = %d, batch size = %d, finished %d\n", 
                data_size, batch_size, batch_size - rest_batch_size - 1);
      }
    }

  private:
    std::unique_ptr<BENCHMARK::Stub> stub_;
};

std::vector<int> Split2Int(const std::string& str, char delim) {
  std::vector<int> ints;
  size_t start;
  size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    ints.push_back(stoi(str.substr(start, end - start)));
  }
  return ints;
}

DEFINE_string(server_address, "localhost:50051", "");
DEFINE_bool(sync_enable, true, "");
DEFINE_bool(async_enable, false, "");
DEFINE_string(platform, "TCP", "which transport protocol used");
DEFINE_string(verbosity, "WARNING", "");
DEFINE_string(data_sizes, "1024,2048", "");
DEFINE_string(batch_sizes, "100,200", "");

int main(int argc, char** argv) {
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

  BenchmarkClient client(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

  if (client.SyncSayHello()) printf("SyncSayHello succeed\n");

  for (int data_size : data_sizes) {
    for (int batch_size : batch_sizes) {
      client.BatchOperations(batch_size, data_size);
    }
  }

  return 0;
}