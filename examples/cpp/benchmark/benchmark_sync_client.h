#ifndef BENCHMARK_SYNC_CLIENT
#define BENCHMARK_SYNC_CLIENT
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
#include "benchmark_utils.h"

#ifdef BAZEL_BUILD
#include "examples/protos/benchmark.grpc.pb.h"
#else

#include "benchmark.grpc.pb.h"

#endif

using std::unique_ptr;
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


class BenchmarkSyncClient {
  public:
    BenchmarkSyncClient(std::shared_ptr<Channel> channel)
      : stub_(BENCHMARK::NewStub(channel)) {}
    
    // sync services
    void SyncSayHello();
    void SyncUnary(size_t batch_size, size_t _request_size_, size_t _reply_size_);
    void SyncClientStream(size_t batch_size, size_t _request_size_);
    void SyncServerStream(size_t batch_size, size_t _reply_size_);
    void SyncBiStream(size_t batch_size, size_t _request_size_,
                      size_t _reply_size_);

  private:
    unique_ptr<BENCHMARK::Stub> stub_;
    TimerPackage timer_;
};

void BenchmarkSyncClient::SyncSayHello() {
  ClientContext context;
  Data_Empty request, reply;
  Status status = stub_->SayHello(&context, request, &reply);
  if (!status.ok()) {
    printf("SyncSayHello failed: not ok\n");
    abort();
  }
}

void BenchmarkSyncClient::SyncUnary(size_t batch_size, size_t _request_size_,
                                size_t _reply_size_) {
  Complex request;
  size_t min_request_size = _request_size_ / 2,
          max_request_size = _request_size_ * 2;
  size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
  size_t request_size, reply_size;
  for (size_t i = 0; i < batch_size; i++) {
    // timer_.Start("SyncUnary: %d", i);
    ClientContext context;
    Complex reply;
    // request_size = random(min_request_size, max_request_size);
    // reply_size = random(min_reply_size, max_reply_size);
    request_size = _request_size_;
    reply_size = _reply_size_;
    request.mutable_datas()->mutable_data1()->resize(request_size);
    request.mutable_numbers()->set_number1(reply_size);
    if (!stub_->Unary(&context, request, &reply).ok()) {
      printf("SyncUnary failed: not ok\n");
      abort();
    }
    if (reply.datas().data1().length() != reply_size) {
      printf(
          "SyncUnary failed: actual reply size != expected reply size\n");
      abort();
    }
    // timer_.Stop();
    // printf("SyncUnary succeed\n");
  }
}

void BenchmarkSyncClient::SyncClientStream(size_t batch_size, size_t _request_size_) {
  ClientContext context;
  Complex request, reply;
  size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
  size_t request_size;
  size_t total_request_size = 0;
  unique_ptr<ClientWriter<Complex>> writer(
      stub_->ClientStream(&context, &reply));
  for (size_t i = 0; i < batch_size; i++) {
    request_size = random(min_request_size, max_request_size);
    request.mutable_datas()->mutable_data1()->resize(request_size);
    if (!writer->Write(request)) {
      printf("SyncClientStream failed: the stream has been closed\n");
      abort();
    }
    total_request_size += request_size;
  }
  writer->WritesDone();
  if (!writer->Finish().ok()) {
    printf("SyncClientStream failed: no ok after write done\n");
    abort();
  }
  if (reply.numbers().number1() != total_request_size) {
    printf(
        "SyncClientStream failed: actual reply size != expected reply "
        "size\n");
    abort();
  }
}

void BenchmarkSyncClient::SyncServerStream(size_t batch_size, size_t _reply_size_) {
  ClientContext context;
  Complex request, reply;
  size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
  size_t actual_batch_size = 0;
  size_t total_reply_size;
  request.mutable_numbers()->set_number1(batch_size);
  request.mutable_numbers()->set_number2(min_reply_size);
  request.mutable_numbers()->set_number3(max_reply_size);
  unique_ptr<ClientReader<Complex>> reader(
      stub_->ServerStream(&context, request));
  while (reader->Read(&reply)) {
    size_t actual_min_reply = reply.numbers().number1();
    size_t actual_max_reply = reply.numbers().number2();
    if (actual_min_reply != min_reply_size ||
        actual_max_reply != max_reply_size) {
      printf(
          "SyncServerStream failed, actual reply size != expected reply "
          "size\n");
      abort();
    }
    actual_batch_size++;
  }
  if (!reader->Finish().ok()) {
    printf("SyncServerStream failed, not ok\n");
    abort();
  }
}

void BenchmarkSyncClient::SyncBiStream(size_t batch_size, size_t _request_size_,
                                   size_t _reply_size_) {
  ClientContext context;
  size_t min_request_size = _request_size_ / 2,
          max_request_size = _request_size_ * 2;
  size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
  size_t request_size, reply_size;
  size_t expected_total_request_size = 0, expected_total_reply_size = 0;

  std::shared_ptr<ClientReaderWriter<Complex, Complex>> stream(
      stub_->BiStream(&context));

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
    }
    stream->WritesDone();
  });

  Complex reply;
  size_t actual_total_request_size = 0, actual_total_reply_size = 0,
          actual_batch_size = 0;
  while (stream->Read(&reply)) {
    actual_batch_size++;
    actual_total_reply_size += reply.datas().data1().length();
    actual_total_request_size += reply.numbers().number1();
  }

  writer.join();
  if (!stream->Finish().ok()) {
    printf("SyncBiStream failed: not ok\n");
    abort();
  }
  if (actual_batch_size != batch_size ||
      actual_total_request_size != expected_total_request_size ||
      actual_total_reply_size != expected_total_reply_size) {
    printf("SyncBiStream failed: actual reply size != expected reply size\n");
    abort();
  }
}





#endif // #ifndef BENCHMARK_SYNC_CLIENT