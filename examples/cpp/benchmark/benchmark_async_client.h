#ifndef BENCHMARK_ASYNC_CLIENT
#define BENCHMARK_ASYNC_CLIENT
#include <memory>
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
using std::shared_ptr;
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

enum ServiceType { SAYHELLO, UNARY, CLIENTSTREAM, SERVERSTREAM, BISTREAM, SERVICE_NUM };
typedef struct AsyncServicesTag AsyncServicesTag;

static std::atomic_int64_t rpc_count{0};

class BenchmarkAsyncClient {
  public:
    BenchmarkAsyncClient(std::shared_ptr<Channel> channel)
      : stub_(BENCHMARK::NewStub(channel)) {}

    void AsyncSayHelloStart(CompletionQueue* cq);

    // initiate a batch of AsyncUnary calls, return after start all RPC calls
    void AsyncUnaryStart(CompletionQueue* cq, size_t batch_size, size_t _request_size_, size_t _reply_size_);

    void AsyncClientStreamStart(CompletionQueue* cq, size_t batch_size, size_t _request_size_);

    // blocking call, the caller thread will keep fetching event from cq, and Proceed it.
    static void NextAndProcees(CompletionQueue* cq);

  private:
    std::unique_ptr<BENCHMARK::Stub> stub_;
};

class AsyncSayHelloService {
  public:
    enum ClientAsyncResponseReaderStatus { CREATE, FINISHED };
    AsyncSayHelloService (BENCHMARK::Stub* stub, CompletionQueue* cq);
    void Start ();
    void Proceed();

  private:
    void Create();
    void Finished();

    // only used in Create()
    BENCHMARK::Stub* stub_;
    CompletionQueue* cq_;

    unique_ptr<AsyncServicesTag> tag_;
    ClientContext ctx_;
    std::unique_ptr<ClientAsyncResponseReader<Data_Empty>> responder_;
    Data_Empty reply_;
    ClientAsyncResponseReaderStatus status_;
    Status finished_status_;
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

    unique_ptr<AsyncServicesTag> tag_;
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
    AsyncClientStreamService (BENCHMARK::Stub* stub, CompletionQueue* cq);
    void Start (std::function<void(Complex*)> request_func, std::function<bool(Complex*)> reply_func, size_t batch_size);
    void Proceed(bool ok = true);

  private:
    void Create();
    void Write();
    void WritesDone();
    void Finishing();
    void Finished();

    // only used in Create()
    BENCHMARK::Stub* stub_;
    CompletionQueue* cq_;

    unique_ptr<AsyncServicesTag> tag_;
    ClientContext ctx_;
    std::unique_ptr<ClientAsyncWriter<Complex>> writer_;
    Complex reply_;
    ClientAsyncWriterStatus status_;
    Status finished_status_;

    std::function<void(Complex*)> request_func_;
    std::function<bool(Complex*)> reply_func_;
    size_t batch_size_ = 0;
};


struct AsyncServicesTag {
  AsyncServicesTag(ServiceType type) : type_(type) {}
  const ServiceType type_;
  void* service_ = nullptr;
  void Proceed(bool ok) {
    switch (type_) {
      case SAYHELLO:
        return static_cast<AsyncSayHelloService*>(service_)->Proceed();
      case UNARY:
        return static_cast<AsyncUnaryService*>(service_)->Proceed();
      case CLIENTSTREAM:
        return static_cast<AsyncClientStreamService*>(service_)->Proceed(ok);
    }
  }
};


// -----< BenchmarkAsyncClient >-----
void BenchmarkAsyncClient::NextAndProcees(CompletionQueue* cq) {
  void* void_tag;
  bool ok = false;
  while (cq->Next(&void_tag, &ok)) {
    AsyncServicesTag* tag = static_cast<AsyncServicesTag*>(void_tag);
    // GPR_ASSERT(ok);
    tag->Proceed(ok);
  }
}

void BenchmarkAsyncClient::AsyncSayHelloStart(CompletionQueue* cq) {
  AsyncSayHelloService* service = new AsyncSayHelloService(stub_.get(), cq);
  service->Start();
  service = nullptr;
}

void BenchmarkAsyncClient::AsyncUnaryStart(CompletionQueue* cq, size_t batch_size, size_t _request_size_, size_t _reply_size_) {
  size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
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
    AsyncUnaryService* service = new AsyncUnaryService(stub_.get(), cq);
    service->Start(request_func, reply_func);
    service = nullptr;
  }
}

void BenchmarkAsyncClient::AsyncClientStreamStart(CompletionQueue* cq, size_t batch_size, size_t _request_size_) {
  size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
  size_t *total_request_size = new size_t;
  *total_request_size = 0;
  auto request_func = [min_request_size, max_request_size, total_request_size](Complex* request)
  {
    size_t request_size = random(min_request_size, max_request_size);
    (*total_request_size) += request_size;
    request->mutable_datas()->mutable_data1()->resize(request_size);
    // printf("total request = %lld\n", *total_request_size);
    return;
  };
  auto reply_func = [total_request_size](Complex* reply)
  {
    bool ret = false;
    // printf("reply = %lld, total request = %lld\n", reply->numbers().number1(), *total_request_size);
    if (reply->numbers().number1() == *total_request_size) {
      ret = true;
    }
    delete total_request_size;
    return ret;
  };

  AsyncClientStreamService* service = new AsyncClientStreamService(stub_.get(), cq);
  service->Start(request_func, reply_func, batch_size);
  service = nullptr;
}


// -----< AsyncSayHelloService >-----
AsyncSayHelloService::AsyncSayHelloService(BENCHMARK::Stub* stub, CompletionQueue* cq)
  : stub_(stub), cq_(cq), tag_(new AsyncServicesTag(SAYHELLO)) {
  tag_->service_ = static_cast<void*>(this); 
}

void AsyncSayHelloService::Start() {
  status_ = CREATE;
  Proceed();
}

void AsyncSayHelloService::Proceed() {
  switch (status_) {
    case CREATE:
      return Create();
    case FINISHED:
      return Finished();
  }
  printf("unknown status: %d\n", status_);
}

void AsyncSayHelloService::Create() {
  rpc_count.fetch_add(1);
  status_ = FINISHED;
  Data_Empty request;
  responder_ = stub_->AsyncSayHello(&ctx_, request, cq_);
  responder_->Finish(&reply_, &finished_status_, tag_.get());
}

void AsyncSayHelloService::Finished() {
  if (!finished_status_.ok()) {
    printf("AsyncSayHelloService failed\n");
  } else {
    // printf("rank %d: AsyncSayHelloService succeed\n", world_rank);
  }
  tag_->service_ = nullptr;
  if (rpc_count.fetch_add(-1) == 1) {
    cq_->Shutdown();
  }
  delete this;
}

// -----< AsyncUnaryService >-----
AsyncUnaryService::AsyncUnaryService(BENCHMARK::Stub* stub, CompletionQueue* cq)
  : stub_(stub), cq_(cq), tag_(new AsyncServicesTag(UNARY)) {
  tag_->service_ = static_cast<void*>(this); 
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
  rpc_count.fetch_add(1);
  status_ = FINISHED;
  Complex request;
  request_func_(&request);
  responder_ = stub_->AsyncUnary(&ctx_, request, cq_);
  responder_->Finish(&reply_, &finished_status_, tag_.get());
}

void AsyncUnaryService::Finished() {
  if (!finished_status_.ok() || !reply_func_(&reply_)) {
    printf("AsyncUnaryService failed\n");
  } else {
    // printf("rank %d: AsyncUnaryService succeed\n", world_rank);
  }
  tag_->service_ = nullptr;
  if (rpc_count.fetch_add(-1) == 1) {
    cq_->Shutdown();
  }
  delete this;
}


// -----< AsyncClientStreamService >-----
AsyncClientStreamService::AsyncClientStreamService(BENCHMARK::Stub* stub, CompletionQueue* cq) 
  : stub_(stub), cq_(cq), tag_(new AsyncServicesTag(CLIENTSTREAM)) { 
  tag_->service_ = static_cast<void*>(this); 
}

void AsyncClientStreamService::Start(std::function<void(Complex*)> request_func, std::function<bool(Complex*)> reply_func, size_t batch_size) {
  request_func_ = request_func;
  reply_func_ = reply_func;
  batch_size_ = batch_size;
  status_ = CREATE;
  Proceed();
}

void AsyncClientStreamService::Proceed(bool ok) {
  // printf("AsyncClientStreamService::Proceed, %d %d, %d\n", batch_size_, status_, ok);
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
  rpc_count.fetch_add(1);
  status_ = WRITE;
  writer_ = stub_->AsyncClientStream(&ctx_, &reply_, cq_, tag_.get());
}

void AsyncClientStreamService::Write() {
  Complex request;
  request_func_(&request);
  if (--batch_size_ > 0) {
    status_ = WRITE;
  } else {
    status_ = WRITESDONE;
  }
  writer_->Write(request, tag_.get());
}

void AsyncClientStreamService::WritesDone() {
  status_ = FINISHING;
  writer_->WritesDone(tag_.get());
}

void AsyncClientStreamService::Finishing() {
  status_ = FINISHED;
  writer_->Finish(&finished_status_, tag_.get());
}

void AsyncClientStreamService::Finished() {
  if (!finished_status_.ok() || !reply_func_(&reply_)) {
    printf("AsyncClientStreamService failed, %d\n", finished_status_.ok());
  } else {
    // printf("AsyncClientStreamService succeed\n");
  }
  tag_->service_ = nullptr;
  if (rpc_count.fetch_add(-1) == 1) {
    cq_->Shutdown();
  }
  delete this;
}







#endif // #ifndef BENCHMARK_ASYNC_CLIENT