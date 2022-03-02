
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

enum ServiceType { SAYHELLO, UNARY, CLIENTSTREAM, SERVERSTREAM, BISTREAM, SERVICE_NUM };

class AsyncSayHelloService;
class AsyncUnaryService;
class AsyncClientStreamService;
class AsyncServerStreamService;
class AsyncBiStreamService;
struct AsyncServicesTag;
typedef struct AsyncServicesTag AsyncServicesTag;

class BenchmarkClient {
  public:
    BenchmarkClient(std::shared_ptr<Channel> channel)
      : stub_(BENCHMARK::NewStub(channel)) {}
    
    // sync services
    void SyncSayHello();
    void SyncUnary(size_t batch_size, size_t _request_size_,
                   size_t _reply_size_);
    void SyncClientStream(size_t batch_size, size_t _request_size_);
    void SyncServerStream(size_t batch_size, size_t _reply_size_);
    void SyncBiStream(size_t batch_size, size_t _request_size_,
                      size_t _reply_size_);
    void BatchSyncs(const size_t batch_size, const size_t data_size);

    // async services
    void AsyncUnaryStart(size_t batch_size, CompletionQueue* cq,
                         size_t _request_size_, size_t _reply_size_);
    void BatchAsyncs(const size_t batch_size, const size_t data_size);

  private:
    static void NextAndProceed(CompletionQueue* cq);
    std::unique_ptr<BENCHMARK::Stub> stub_;
};


class AsyncUnaryService {
  public:
    enum ClientAsyncResponseReaderStatus { CREATE, FINISHED };
    AsyncUnaryService (BENCHMARK::Stub* stub, CompletionQueue* cq);
    void Start (std::function<void(Complex*)> request_func, std::function<bool(Complex*)> reply_func);
    void Proceed();

  private:
    void Create();
    void Finished();

    // only used in Create()
    BENCHMARK::Stub* stub_;
    CompletionQueue* cq_;

    AsyncServicesTag* tag_;
    ClientContext ctx_;
    std::unique_ptr<ClientAsyncResponseReader<Complex>> responder_;
    Complex reply_;
    ClientAsyncResponseReaderStatus status_;
    Status finished_status_;

    std::function<void(Complex*)> request_func_;
    std::function<bool(Complex*)> reply_func_;
};


class AsyncClientStreamService {
  public:
    enum ClientAsyncWriterStatus { CREATE, WRITE, WRITESDONE, FINISHING, FINISHED};
    // typedef bool (*RequestFunc)(Complex* request); // fill request, return true if WritesDone
    // typedef bool (*ReplyFunc)(Complex* reply); // process reply, return true if ack succeed
    AsyncClientStreamService (BENCHMARK::Stub* stub, CompletionQueue* cq);
    void Start (std::function<bool(Complex*)> request_func, std::function<bool(Complex*)> reply_func);
    void Proceed();

  private:
    void Create();
    void Write();
    void WritesDone();
    void Finishing();
    void Finished();

    // only used in Create()
    BENCHMARK::Stub* stub_;
    CompletionQueue* cq_;

    AsyncServicesTag* tag_;
    ClientContext ctx_;
    std::unique_ptr<ClientAsyncWriter<Complex>> writer_;
    Complex reply_;
    ClientAsyncWriterStatus status_;
    Status finished_status_;

    std::function<bool(Complex*)> request_func_;
    std::function<bool(Complex*)> reply_func_;
};


struct AsyncServicesTag {
  AsyncServicesTag(ServiceType type) : type_(type) {}
  const ServiceType type_;
  // AsyncSayHelloService* say_hello_service_ = nullptr;
  AsyncUnaryService* unary_service_ = nullptr;
  AsyncClientStreamService* client_stream_service_ = nullptr;
  // AsyncServerStreamService* server_stream_service_ = nullptr;
  // AsyncBiStreamService* bi_stream_service_ = nullptr;
  void Proceed() {
    switch (type_) {
      case CLIENTSTREAM:
        return client_stream_service_->Proceed();
      case UNARY:
        return unary_service_->Proceed();
    }
  }
};


// -----< BenchmarkClient >-----
void BenchmarkClient::SyncSayHello() {
  ClientContext context;
  Data_Empty request, reply;
  Status status = stub_->SayHello(&context, request, &reply);
  if (!status.ok()) {
    printf("SyncSayHello failed: not ok\n");
    abort();
  }
}

void BenchmarkClient::SyncUnary(size_t batch_size, size_t _request_size_,
                                size_t _reply_size_) {
  Complex request;
  size_t min_request_size = _request_size_ / 2,
          max_request_size = _request_size_ * 2;
  size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
  size_t request_size, reply_size;
  for (size_t i = 0; i < batch_size; i++) {
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
    // printf("SyncUnary succeed\n");
  }
}

void BenchmarkClient::SyncClientStream(size_t batch_size, size_t _request_size_) {
  ClientContext context;
  Complex request, reply;
  size_t min_request_size = _request_size_ / 2,
          max_request_size = _request_size_ * 2;
  size_t request_size;
  size_t total_request_size = 0;
  std::unique_ptr<ClientWriter<Complex>> writer(
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

void BenchmarkClient::SyncServerStream(size_t batch_size, size_t _reply_size_) {
  ClientContext context;
  Complex request, reply;
  size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
  size_t actual_batch_size = 0;
  size_t total_reply_size;
  request.mutable_numbers()->set_number1(batch_size);
  request.mutable_numbers()->set_number2(min_reply_size);
  request.mutable_numbers()->set_number3(max_reply_size);
  std::unique_ptr<ClientReader<Complex>> reader(
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

void BenchmarkClient::SyncBiStream(size_t batch_size, size_t _request_size_,
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

void BenchmarkClient::BatchSyncs(const size_t batch_size, const size_t data_size) {
  std::stringstream ss;
  size_t us;

  auto t0 = std::chrono::high_resolution_clock::now();
  SyncUnary(batch_size, data_size / 2, data_size * 2);
  auto t1 = std::chrono::high_resolution_clock::now();
  us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  ss.str("");
  ss << "SyncUnary, batch size = " << batch_size
      << ", data size = " << data_size;
  MPI_summary_time(us, ss.str().c_str(), "us");
  MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");

  // auto t2 = std::chrono::high_resolution_clock::now();
  // SyncClientStream(batch_size, data_size);
  // auto t3 = std::chrono::high_resolution_clock::now();
  // us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
  // ss.str("");
  // ss << "SyncClientStream, batch size = " << batch_size
  //     << ", data size = " << data_size;
  // MPI_summary_time(us, ss.str().c_str(), "us");
  // MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");

  // auto t4 = std::chrono::high_resolution_clock::now();
  // SyncServerStream(batch_size, data_size);
  // auto t5 = std::chrono::high_resolution_clock::now();
  // us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
  // ss.str("");
  // ss << "SyncServerStream, batch size = " << batch_size
  //     << ", data size = " << data_size;
  // MPI_summary_time(us, ss.str().c_str(), "us");
  // MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");

  // auto t6 = std::chrono::high_resolution_clock::now();
  // SyncBiStream(batch_size, data_size / 2, data_size * 2);
  // auto t7 = std::chrono::high_resolution_clock::now();
  // us = std::chrono::duration_cast<std::chrono::microseconds>(t7 - t6).count();
  // ss.str("");
  // ss << "SyncBiStream, batch size = " << batch_size
  //     << ", data size = " << data_size;
  // MPI_summary_time(us, ss.str().c_str(), "us");
  // MPI_Summary_throughput(double(batch_size) / us * 1000000, ss.str().c_str(), "rpcs/s");
}

void BenchmarkClient::NextAndProceed(CompletionQueue* cq) {
  void* tag = nullptr;
  bool ok = false;
  while (cq->Next(&tag, &ok)) {
    GPR_ASSERT(ok);
    static_cast<AsyncUnaryService*>(tag)->Proceed();
  }
}

void BenchmarkClient::AsyncUnaryStart(size_t batch_size, CompletionQueue* cq, 
                                      size_t _request_size_, size_t _reply_size_) {
  size_t min_request_size = _request_size_ / 2,
          max_request_size = _request_size_ * 2;
  size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
  size_t reply_size = random(min_reply_size, max_reply_size);
  auto request_func = [=](Complex* request)
  {
    size_t request_size = random(min_request_size, max_request_size);
    request->mutable_datas()->mutable_data1()->resize(request_size);
    request->mutable_numbers()->set_number1(reply_size);
    return;
  };
  auto reply_func = [=](Complex* reply)
  {
    return reply->datas().data1().length() == reply_size;
  };

  while (batch_size--) {
    AsyncUnaryService* unary = new AsyncUnaryService(&(*stub_), cq);
    unary->Start(request_func, reply_func);
  }  

}

void BenchmarkClient::BatchAsyncs(const size_t batch_size, const size_t data_size) {
  CompletionQueue cq;
  std::thread worker_thread = std::thread(NextAndProceed, &cq);
  AsyncUnaryStart(batch_size, &cq, data_size / 2, data_size * 2);
}

// -----< AsyncUnaryService >-----
AsyncUnaryService::AsyncUnaryService(BENCHMARK::Stub* stub, CompletionQueue* cq)
  : stub_(stub), cq_(cq) { 
  tag_ = new AsyncServicesTag(UNARY);
  tag_->unary_service_ = this; 
}

void AsyncUnaryService::Start(std::function<void(Complex*)> request_func, std::function<bool(Complex*)> reply_func) {
  request_func_ = request_func;
  reply_func_ = reply_func;
  status_ = CREATE;
  Proceed();
}

void AsyncUnaryService::Proceed() {
  switch (status_) {
    case CREATE:
      return Create();
    case FINISHED:
      return Finished();
  }
  printf("unknown status: %d\n", status_);
}

void AsyncUnaryService::Create() {
  status_ = FINISHED;
  Complex request;
  request_func_(&request);
  responder_ = stub_->AsyncUnary(&ctx_, request, cq_);
  responder_->Finish(&reply_, &finished_status_, tag_);
}

void AsyncUnaryService::Finished() {
  if (!finished_status_.ok() || !reply_func_(&reply_)) {
    printf("AsyncUnaryService failed");
  } else {
    printf("AsyncUnaryService succeed\n");
  }
  tag_->unary_service_ = nullptr;
  delete tag_;
  delete this;
}


// -----< AsyncClientStreamService >-----
AsyncClientStreamService::AsyncClientStreamService(BENCHMARK::Stub* stub, CompletionQueue* cq) 
  : stub_(stub),
    cq_(cq) { 
  tag_ = new AsyncServicesTag(CLIENTSTREAM);
  tag_->client_stream_service_ = this; 
}

void AsyncClientStreamService::Start(std::function<bool(Complex*)> request_func, std::function<bool(Complex*)> reply_func) {
  request_func_ = request_func;
  reply_func_ = reply_func;
  status_ = CREATE;
  Proceed();
}

void AsyncClientStreamService::Proceed() {
  switch (status_) {
    case CREATE:
      return Create();
    case WRITE:
      return Write();
    case WRITESDONE:
      return WritesDone();
    case FINISHING:
      return Finishing();
    case FINISHED:
      return Finished();
  }
  printf("unknown status: %d\n", status_);
}

void AsyncClientStreamService::Create() {
  status_ = WRITE;
  writer_ = stub_->AsyncClientStream(&ctx_, &reply_, cq_, tag_);
}

void AsyncClientStreamService::Write() {
  Complex request;
  status_ = WRITE;
  if (request_func_(&request)) {
    status_ = WRITESDONE;
  }
  writer_->Write(request, tag_);
}

void AsyncClientStreamService::WritesDone() {
  status_ = FINISHING;
  writer_->WritesDone(tag_);
}

void AsyncClientStreamService::Finishing() {
  status_ = FINISHED;
  writer_->Finish(&finished_status_, tag_);
}

void AsyncClientStreamService::Finished() {
  if (!finished_status_.ok() || !reply_func_(&reply_)) {
    printf("AsyncClientStreamService failed\n");
  } else {
    printf("AsyncClientStreamService succeed\n");
  }
  tag_->client_stream_service_ = nullptr;
  delete tag_;
  delete this;
}




























