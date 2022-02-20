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

    bool SyncBiUnary(size_t batch_size, size_t _request_size_, size_t _reply_size_) {
      Complex request;
      size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
      size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
      size_t request_size, reply_size;
      for (size_t i = 0; i < batch_size; i++) {
        ClientContext context;
        Complex reply;
        request_size = random(min_request_size, max_request_size);
        reply_size = random(min_reply_size, max_reply_size);
        request.mutable_datas()->mutable_data1()->resize(request_size);
        request.mutable_numbers()->set_number1(reply_size);
        if (!stub_->BiUnary(&context, request, &reply).ok()) {
          printf("SyncBiUnary failed: not ok, i = %d, batch size = %d, request size in [%d, %d], reply size in [%d, %d]\n",
            i, batch_size, min_request_size, max_request_size, min_reply_size, max_reply_size);
          return false;
        }
        if (reply.datas().data1().length() != reply_size) {
          printf("SyncBiUnary failed: expected reply size = %d, actual reply size = %d, i = %d, batch size = %d, request size in [%d, %d], reply size in [%d, %d]\n",
            reply_size, reply.datas().data1().length(), i, batch_size, min_request_size, max_request_size, min_reply_size, max_reply_size);
          return false;
        }
      }
      printf("SyncBiUnary succeed: batch size = %d, request size in [%d, %d], reply size in [%d, %d]\n",
              batch_size, min_request_size, max_request_size, min_reply_size, max_reply_size);
      return true;

    }

    bool SyncClientStream(size_t batch_size, size_t _request_size_) {
      ClientContext context;
      Complex request, reply;
      size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
      size_t request_size;
      size_t total_request_size = 0;
      std::unique_ptr<ClientWriter<Complex>> writer(stub_->ClientStream(&context, &reply));
      for (size_t i = 0; i < batch_size; i++) {
        request_size = random(min_request_size, max_request_size);
        request.mutable_datas()->mutable_data1()->resize(request_size);
        if (!writer->Write(request)) {
          printf("SyncClientStream failed: the stream has been closed, i = %d, batch size = %d, request size = %d, total request size so far = %lld\n",
                  i, batch_size, request_size, total_request_size);
          return false;
        }
        total_request_size += request_size;
      }
      writer->WritesDone();
      if (!writer->Finish().ok()) {
        printf("SyncClientStream failed: no ok after write done, batch size = %d, request size in [%d, %d]\n",
                batch_size, min_request_size, max_request_size);
        return false;
      }
      if (reply.numbers().number1() == total_request_size && reply.numbers().number2() == batch_size) {
        printf("SyncClientStream succeed: batch size = %d, request size in [%d, %d]\n",
                batch_size, min_request_size, max_request_size);
        return true;
      } else {
        printf("SyncClientStream failed: batch size = %d, reply batch size = %d, request size in [%d, %d], total request size = %lld, reply total reqeust size = %lld\n",
                batch_size, reply.numbers().number1(), min_request_size, max_request_size, total_request_size, reply.numbers().number2());
        return false;
      }
    }

    bool SyncBiStream(size_t batch_size, size_t _request_size_, size_t _reply_size_) {
      ClientContext context;
      size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
      size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
      size_t request_size, reply_size;
      size_t expected_total_request_size = 0, expected_total_reply_size = 0;

      std::shared_ptr<ClientReaderWriter<Complex, Complex>> stream(stub_->BiStream(&context));

      std::thread writer([&]() {
        Complex request;
        for (int i = 0; i < batch_size; i++) {
          request_size = random(min_request_size, max_request_size);
          reply_size = random(min_reply_size, max_reply_size);
          request.mutable_datas()->mutable_data1()->resize(request_size);
          request.mutable_numbers()->set_number1(reply_size);
          stream->Write(request);
          expected_total_request_size += request_size;
          expected_total_reply_size += reply_size;
          printf("write %d done\n", i + 1);
        }
        stream->WritesDone();
      });

      Complex reply;
      size_t actual_total_request_size = 0, actual_total_reply_size = 0, actual_batch_size = 0;
      while (stream->Read(&reply)) {
        actual_batch_size++;
        actual_total_reply_size += reply.datas().data1().length();
        actual_total_request_size += reply.numbers().number1();
        printf("\t read %d done\n", actual_batch_size);
      }

      writer.join();
      if (!stream->Finish().ok()) {
        printf("SyncBiStream failed: not ok, batch size = %d, request size in [%d, %d], reply size in [%d, %d]\n",
                batch_size, min_request_size, max_request_size, min_reply_size, max_reply_size);
        return false;
      }
      if (actual_batch_size == batch_size &&
          actual_total_request_size == expected_total_request_size &&
          actual_total_reply_size == expected_total_reply_size) {
        printf("SyncBiStream succeed, batch size = %d, request size in [%d, %d], reply size in [%d, %d]\n",
                batch_size, min_request_size, max_request_size, min_reply_size, max_reply_size);
        return true;
      } else {
        printf("SyncBiStream failed: batch size = %d, reply batch size = %d\n", batch_size, actual_batch_size);
        printf("\t request size in [%d, %d], total request size = %d, reply total request size = %d\n", 
                min_request_size, max_request_size, expected_total_request_size, actual_total_request_size);
        printf("\t reply size in [%d, %d], total reply size = %d, reply total reply size = %d\n", 
                min_reply_size, max_reply_size, expected_total_reply_size, actual_total_reply_size);
        return false;
      }
    }

    void BatchOperations(const size_t batch_size, const size_t data_size) {
      printf("\nbatch starts: batch size = %d, data size = %d\n", batch_size, data_size);
      if (!SyncBiUnary(batch_size, data_size / 2, data_size * 2)) return;
      if (!SyncClientStream(batch_size, data_size)) return;
      // if (!SyncBiStream(batch_size, data_size / 2, data_size * 2)) return;
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
DEFINE_string(data_sizes, "1024", "");
DEFINE_string(batch_sizes, "1000", "");

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