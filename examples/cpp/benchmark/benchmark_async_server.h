#ifndef BENCHMARK_ASYNC_SERVER
#define BENCHMARK_ASYNC_SERVER
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
#include "benchmark_utils.h"

#ifdef BAZEL_BUILD
#include "examples/protos/benchamrk.grpc.pb.h"
#else

#include "benchmark.grpc.pb.h"

#endif

using std::shared_ptr;
using std::unique_ptr;
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

enum ServiceType { SAYHELLO, UNARY, CLIENTSTREAM, SERVERSTREAM, BISTREAM };
typedef struct AsyncServicesTag AsyncServicesTag;

class BenchmarkAsyncServer {
  public:
    BenchmarkAsyncServer(const std::string server_address);

    void Run(size_t thread_num, size_t cq_num);

    void AsyncSayHelloStart(CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);
    void AsyncUnaryStart(CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);
    void AsyncClientStreamStart(CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);

    unique_ptr<ServerCompletionQueue> AddServerCompletionQueue() { return builder_->AddCompletionQueue(); }
    static void NextAndProceed(CompletionQueue* CPU_EQUAL);

  private:
    unique_ptr<BENCHMARK::AsyncService> async_service_;
    unique_ptr<ServerBuilder> builder_;
    unique_ptr<grpc::Server> server_;
    TimerPackage timer_;
};

class AsyncSayHelloService {
  public:
    enum ServerAsyncResponseWriterStatus { CREATE, PROCESS, FINISH };
    AsyncSayHelloService (BENCHMARK::AsyncService* service, 
                       CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);
    void Start ();
    void Proceed();

  private:
    void Create();
    void Process();
    void Finish();

    // only used in Create()
    // BENCHMARK::Stub* stub_;
    BENCHMARK::AsyncService* service_;
    ServerCompletionQueue* notification_cq_;
    CompletionQueue* call_cq_;

    unique_ptr<AsyncServicesTag> tag_;
    ServerContext ctx_;
    ServerAsyncResponseWriter<Data_Empty> responder_;
    Data_Empty request_;
    Data_Empty reply_;
    ServerAsyncResponseWriterStatus status_;
    Status finished_status_;
};

class AsyncUnaryService {
  public:
    enum ServerAsyncResponseWriterStatus { CREATE, PROCESS, FINISH };
    AsyncUnaryService (BENCHMARK::AsyncService* service, 
                       CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);
    void Start (std::function<void(Complex*, Complex*)> process_func);
    void Proceed();

  private:
    void Create();
    void Process();
    void Finish();

    // only used in Create()
    // BENCHMARK::Stub* stub_;
    BENCHMARK::AsyncService* service_;
    ServerCompletionQueue* notification_cq_;
    CompletionQueue* call_cq_;

    unique_ptr<AsyncServicesTag> tag_;
    ServerContext ctx_;
    ServerAsyncResponseWriter<Complex> responder_;
    Complex request_;
    Complex reply_;
    ServerAsyncResponseWriterStatus status_;
    Status finished_status_;

    std::function<void(Complex*, Complex*)> process_func_;
};

class AsyncClientStreamService {
  public:
    enum ServerAsyncReaderStatus { CREATE, FIRSTREAD, READ, FINISH };
    AsyncClientStreamService (BENCHMARK::AsyncService* service, 
                       CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);
    void Start (std::function<void(Complex*)> request_func, std::function<void(Complex*)> reply_func);
    void Proceed(bool ok = true);

  private:
    void Create();
    void FirstRead();
    void Read();
    void ReadDone();
    void Finish();

    // only used in Create()
    // BENCHMARK::Stub* stub_;
    BENCHMARK::AsyncService* service_;
    ServerCompletionQueue* notification_cq_;
    CompletionQueue* call_cq_;

    unique_ptr<AsyncServicesTag> tag_;
    ServerContext ctx_;
    ServerAsyncReader<Complex, Complex> reader_;
    Complex request_;
    Complex reply_;
    ServerAsyncReaderStatus status_;
    Status finished_status_;
    std::mutex read_mu_;

    std::function<void(Complex*)> request_func_;
    std::function<void(Complex*)> reply_func_;
};

class AsyncServerStreamService {
  public:
    enum ServerAsyncWriterStatus { CREATE, WRITE, WRITEDONE, FINISH };
    AsyncServerStreamService (BENCHMARK::AsyncService* service, 
                       CompletionQueue* call_cq, ServerCompletionQueue* notification_cq);
    void Start (std::function<void(Complex*)> request_func, std::function<bool(Complex*)> reply_func);
    void Proceed();

  private:
    void Create();
    void Write();
    void WritesDone();
    void Finish();

    BENCHMARK::AsyncService* service_;
    ServerCompletionQueue* notification_cq_;
    CompletionQueue* call_cq_;

    unique_ptr<AsyncServicesTag> tag_;
    ServerContext ctx_;
    ServerAsyncWriter<Complex> writer_;
    Complex request_;
    Complex reply_;
    ServerAsyncWriterStatus status_;
    Status finished_status_;
    std::mutex read_mu_;

    std::function<void(Complex*)> request_func_;
    std::function<bool(Complex*)> reply_func_;
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

// -----< BenchmarkAsyncServer >-----
BenchmarkAsyncServer::BenchmarkAsyncServer(const std::string server_address) 
  : builder_(new ServerBuilder()), async_service_(new BENCHMARK::AsyncService()){
  builder_->AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder_->RegisterService(async_service_.get());
  // server_ = builder_->BuildAndStart();
}

void BenchmarkAsyncServer::Run(size_t thread_num, size_t cq_num) {

  // create ServerCompletionQueue and start server
  std::vector<std::unique_ptr<ServerCompletionQueue>> cqs;
  for (size_t i = 0; i < cq_num; i++) {
    cqs.emplace_back(AddServerCompletionQueue());
  }
  server_ = builder_->BuildAndStart();

  // start all async services on each CQs
  for (size_t i = 0; i < cq_num; i++) {
    AsyncSayHelloStart(cqs[i].get(), cqs[i].get());
    AsyncUnaryStart(cqs[i].get(), cqs[i].get());
    AsyncClientStreamStart(cqs[i].get(), cqs[i].get());
  }


  // start all worker threads
  std::vector<std::thread> workers;
  for (size_t i = 0; i < thread_num; i++) {
    workers.emplace_back(std::thread(NextAndProceed, cqs[i % cq_num].get()));
  }

  for (size_t i = 0; i < thread_num; i++) {
    workers[i].join();
  }
}

void BenchmarkAsyncServer::AsyncSayHelloStart(CompletionQueue* call_cq, ServerCompletionQueue* notification_cq) {
  AsyncSayHelloService* unary = new AsyncSayHelloService(async_service_.get(), call_cq, notification_cq);
  unary->Start();
}

void BenchmarkAsyncServer::AsyncUnaryStart(CompletionQueue* call_cq, ServerCompletionQueue* notification_cq) {
  auto process_func = [](Complex* request, Complex* reply) 
  {
    reply->mutable_datas()->mutable_data1()->resize(request->numbers().number1());
  };
  AsyncUnaryService* unary = new AsyncUnaryService(async_service_.get(), call_cq, notification_cq);
  unary->Start(process_func);
}

void BenchmarkAsyncServer::AsyncClientStreamStart(CompletionQueue* call_cq, ServerCompletionQueue* notification_cq) {
  size_t* total_data_size = new size_t;
  *total_data_size = 0;
  auto request_func = [total_data_size](Complex* request)
  {
    (*total_data_size) += request->datas().data1().length();
    // printf("total request size = %lld\n", *total_data_size);
    return;
  };
  auto reply_func = [total_data_size](Complex* reply)
  {
    // printf("set reply size = %lld\n", *total_data_size);
    reply->mutable_numbers()->set_number1(*total_data_size);
    *total_data_size = 0;
    return;
  };
  AsyncClientStreamService* client_stream = new AsyncClientStreamService(async_service_.get(), call_cq, notification_cq);
  client_stream->Start(request_func, reply_func);
}

void BenchmarkAsyncServer::NextAndProceed(CompletionQueue* cq) {

  void* void_tag;
  bool ok = false;
  while (cq->Next(&void_tag, &ok)) {
    AsyncServicesTag* tag = static_cast<AsyncServicesTag*>(void_tag);
    // GPR_ASSERT(ok);
    tag->Proceed(ok);
  }
}

// -----< AsyncSayHelloService >-----
AsyncSayHelloService::AsyncSayHelloService(BENCHMARK::AsyncService* service, 
                                     CompletionQueue* call_cq, 
                                     ServerCompletionQueue* notification_cq)
  : service_(service), responder_(&ctx_), tag_(new AsyncServicesTag(SAYHELLO)),
    call_cq_(call_cq), notification_cq_(notification_cq) {
  tag_->service_ = static_cast<void*>(this);
}

void AsyncSayHelloService::Start() {
  status_ = CREATE;
  Proceed();
}

void AsyncSayHelloService::Proceed() {
  // printf("thread %lld is proceeding on unary service %d\n", std::this_thread::get_id(), status_);
  switch (status_) {
    case CREATE:
      return Create();
    case PROCESS:
      return Process();
    case FINISH:
      return Finish();
  }
  printf("unknown status: %d\n", status_);
}

void AsyncSayHelloService::Create() {
  status_ = PROCESS;
  service_->RequestSayHello(&ctx_, &request_, &responder_, call_cq_, notification_cq_, tag_.get());
}

void AsyncSayHelloService::Process() {
  AsyncSayHelloService* next = new AsyncSayHelloService(service_, call_cq_, notification_cq_);
  next->Start();
  status_ = FINISH;
  responder_.Finish(reply_, Status::OK, tag_.get());
}

void AsyncSayHelloService::Finish() {
  tag_->service_ = nullptr;
  delete this;
}

// -----< AsyncUnaryService >-----
AsyncUnaryService::AsyncUnaryService(BENCHMARK::AsyncService* service, 
                                     CompletionQueue* call_cq, 
                                     ServerCompletionQueue* notification_cq)
  : service_(service), responder_(&ctx_), tag_(new AsyncServicesTag(UNARY)),
    call_cq_(call_cq), notification_cq_(notification_cq) {
  tag_->service_ = static_cast<void*>(this);
}

void AsyncUnaryService::Start(std::function<void(Complex*, Complex*)> process_func) {
  process_func_ = process_func;
  status_ = CREATE;
  Proceed();
}

void AsyncUnaryService::Proceed() {
  // printf("thread %lld is proceeding on unary service %d\n", std::this_thread::get_id(), status_);
  switch (status_) {
    case CREATE:
      return Create();
    case PROCESS:
      return Process();
    case FINISH:
      return Finish();
  }
  printf("unknown status: %d\n", status_);
}

void AsyncUnaryService::Create() {
  status_ = PROCESS;
  service_->RequestUnary(&ctx_, &request_, &responder_, call_cq_, notification_cq_, tag_.get());
}

void AsyncUnaryService::Process() {
  AsyncUnaryService* next = new AsyncUnaryService(service_, call_cq_, notification_cq_);
  next->Start(process_func_);
  process_func_(&request_, &reply_);
  status_ = FINISH;
  responder_.Finish(reply_, Status::OK, tag_.get());
}

void AsyncUnaryService::Finish() {
  tag_->service_ = nullptr;
  delete this;
}

// -----< AsyncClientStreamService >-----
AsyncClientStreamService::AsyncClientStreamService(BENCHMARK::AsyncService* service, 
                                                   CompletionQueue* call_cq, 
                                                   ServerCompletionQueue* notification_cq)
  : service_(service), reader_(&ctx_), tag_(new AsyncServicesTag(CLIENTSTREAM)),
    call_cq_(call_cq), notification_cq_(notification_cq) {
  tag_->service_ = static_cast<void*>(this);
}

void AsyncClientStreamService::Start(std::function<void(Complex*)> request_func, std::function<void(Complex*)> reply_func) {
  request_func_ = request_func;
  reply_func_ = reply_func;
  status_ = CREATE;
  Proceed();
}

void AsyncClientStreamService::Proceed(bool ok) {
  switch (status_) {
    case CREATE:
      return Create();
    case FIRSTREAD:
      return FirstRead();
    case READ:
      if (ok) return Read();
      else return ReadDone();
    case FINISH:
      return Finish();
  }
  printf("unknown status: %d\n", status_);
}

void AsyncClientStreamService::Create() {
  status_ = FIRSTREAD;
  // ctx_.AsyncNotifyWhenDone()
  service_->RequestClientStream(&ctx_, &reader_, call_cq_, notification_cq_, tag_.get());
}

void AsyncClientStreamService::FirstRead() {
  read_mu_.lock();
  status_ = READ;
  reader_.Read(&request_, tag_.get());
  read_mu_.unlock();
}

void AsyncClientStreamService::Read() {
  read_mu_.lock();
  status_ = READ;
  request_func_(&request_);
  reader_.Read(&request_, tag_.get());
  read_mu_.unlock();
}

void AsyncClientStreamService::ReadDone() {
  AsyncClientStreamService* next = new AsyncClientStreamService(service_, call_cq_, notification_cq_);
  next->Start(request_func_, reply_func_);
  reply_func_(&reply_);
  status_ = FINISH;
  reader_.Finish(reply_, Status::OK, tag_.get());
}

void AsyncClientStreamService::Finish() {
  tag_->service_ = nullptr;
  delete this;
}

// -----< AsyncServerStreamService >-----
AsyncServerStreamService::AsyncServerStreamService(BENCHMARK::AsyncService* service,
                                                   CompletionQueue* call_cq,
                                                   ServerCompletionQueue* notification_cq)
  : service_(service), writer_(&ctx_), tag_(new AsyncServicesTag(SERVERSTREAM)),
    call_cq_(call_cq), notification_cq_(notification_cq) {
  tag_->service_ = static_cast<void*>(this);
}

// void AsyncServerStreamService::Start() {

// }

void AsyncServerStreamService::Create() {

}


#endif // #ifndef BENCHMARK_ASYNC_SERVER