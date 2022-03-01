#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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
class AsyncSayHelloService;
class AsyncUnaryService;
class AsyncClientStreamService;
class AsyncServerStreamService;
class AsyncBiStreamService;
struct AsyncServicesTag;
typedef struct AsyncServicesTag AsyncServicesTag;

class SyncServeice final : public BENCHMARK::Service {
  public:
    Status SayHello(ServerContext* context, const Data_Empty* request, Data_Empty* reply) override;
    Status Unary(ServerContext* context, const Complex* request, Complex* reply) override;
    Status ClientStream(ServerContext* context, ServerReader<Complex>* reader, Complex* reply) override;
    Status ServerStream(ServerContext* context, const Complex* request, ServerWriter<Complex>* writer) override;
    Status BiStream(ServerContext* context, ServerReaderWriter<Complex, Complex>* stream) override;
  private:
    size_t unary_request_total_size = 0;
    size_t unary_reply_total_size = 0;
    size_t unary_batch_size = 0;
};

class BenchmarkServer {
  public:
    BenchmarkServer(const std::string server_address, 
                    bool async_enable, size_t async_threads_num, size_t async_cqs_num);
    
    void Run();

    void AsyncHandleAll();

    void AsyncUnary(CompletionQueue* cqll_cq, ServerCompletionQueue* notification_cq);

  private:
    static void NextAndProceed(CompletionQueue* cq);
    ServerBuilder builder_;
    std::unique_ptr<grpc::Server> server_;

    SyncServeice sync_service_;
    bool sync_enable_ = false;
    size_t sync_threads_num_ = 0;
    std::vector<std::thread> sync_threads_;

    BENCHMARK::AsyncService async_service_;
    bool async_enable_ = false;
    size_t async_threads_num_ = 0, async_cqs_num_ = 0;
    std::vector<std::thread> async_threads_;
    std::vector<std::unique_ptr<ServerCompletionQueue>> async_cqs_;
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

    AsyncServicesTag* tag_;
    ServerContext ctx_;
    ServerAsyncResponseWriter<Complex> responder_;
    Complex request_;
    Complex reply_;
    ServerAsyncResponseWriterStatus status_;
    Status finished_status_;

    std::function<void(Complex*, Complex*)> process_func_;
};




struct AsyncServicesTag {
  AsyncServicesTag(ServiceType type) : type_(type) {}
  const ServiceType type_;
  // AsyncSayHelloService* say_hello_service_ = nullptr;
  AsyncUnaryService* unary_service_ = nullptr;
  // AsyncClientStreamService* client_stream_service_ = nullptr;
  // AsyncServerStreamService* server_stream_service_ = nullptr;
  // AsyncBiStreamService* bi_stream_service_ = nullptr;
  void Proceed() {
    switch (type_) {
      // case CLIENTSTREAM:
      //   return client_stream_service_->Proceed();
      case UNARY:
        return unary_service_->Proceed();
    }
  }
};


// -----< BenchmarkServer >-----
BenchmarkServer::BenchmarkServer(const std::string server_address, 
                                 bool async_enable, size_t async_threads_num, size_t async_cqs_num)
  : async_enable_(async_enable),
    async_threads_num_(async_threads_num),
    async_cqs_num_(async_cqs_num) {
  builder_.AddListeningPort(server_address,
                            grpc::InsecureServerCredentials());
  if (async_enable) {
    printf("Server: async mode, threads num %d, CQs num %d\n", 
            async_threads_num_, async_cqs_num_);
    builder_.RegisterService(&async_service_);
    while (async_cqs_num--) {
      async_cqs_.push_back(builder_.AddCompletionQueue());
    }
  } else {
    printf("Server: sync mode\n");
    builder_.RegisterService(&sync_service_);
  }
  server_ = builder_.BuildAndStart();
  printf("Server is listening on %s\n", server_address.c_str());
}

void BenchmarkServer::AsyncHandleAll() {
  for (size_t i = 0; i < async_cqs_num_; i++) {
    ServerCompletionQueue* cq = async_cqs_[i].get();
    AsyncUnary(cq, cq);
  }

  for (size_t i = 0; i < async_threads_num_; i++) {
    CompletionQueue* cq = async_cqs_[i % async_cqs_num_].get();
    async_threads_.emplace_back(std::thread(NextAndProceed, cq));
  }

  for (size_t i = 0; i < async_threads_num_; i++) {
    async_threads_[i].join();
  }
}

void BenchmarkServer::AsyncUnary(CompletionQueue* cqll_cq, ServerCompletionQueue* notification_cq) {
  auto process_func = [](Complex* request, Complex* reply)
  {
    reply->mutable_datas()->mutable_data1()->resize(request->numbers().number1());
  };
  AsyncUnaryService* unary = new AsyncUnaryService(&async_service_, cqll_cq, notification_cq);
  unary->Start(process_func);

}

void BenchmarkServer::NextAndProceed(CompletionQueue* cq) {
  void* tag = nullptr;
  bool ok = false;
  while (true) {
    GPR_ASSERT(cq->Next(&tag, &ok));
    GPR_ASSERT(ok);
    static_cast<AsyncServicesTag*>(tag)->Proceed();
  }
}

void BenchmarkServer::Run() {
  if (async_enable_) {
    if (async_threads_num_ < async_cqs_num_) {
      std::cerr << "Too few async threads" << std::endl;
      exit(1);
    }
    AsyncHandleAll();
  } else { // sync mode
    server_->Wait();
  }
}


// -----< AsyncUnaryService >-----
AsyncUnaryService::AsyncUnaryService(BENCHMARK::AsyncService* service, 
                                     CompletionQueue* call_cq, ServerCompletionQueue* notification_cq)
  : service_(service), responder_(&ctx_), 
    call_cq_(call_cq), notification_cq_(notification_cq) {
  tag_ = new AsyncServicesTag(UNARY);
  tag_->unary_service_ = this;
}

void AsyncUnaryService::Start(std::function<void(Complex*, Complex*)> process_func) {
  process_func_ = process_func;
  status_ = CREATE;
  Proceed();
}

void AsyncUnaryService::Proceed() {
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
  service_->RequestUnary(&ctx_, &request_, &responder_, call_cq_, notification_cq_, tag_);
}

void AsyncUnaryService::Process() {
  AsyncUnaryService* next = new AsyncUnaryService(service_, call_cq_, notification_cq_);
  next->Start(process_func_);
  process_func_(&request_, &reply_);
  status_ = FINISH;
  responder_.Finish(reply_, Status::OK, tag_);
}

void AsyncUnaryService::Finish() {
  tag_->unary_service_ = nullptr;
  delete tag_;
  delete this;
}




// -----< SyncServeice >-----
Status SyncServeice::SayHello(ServerContext* context, const Data_Empty* request,
                              Data_Empty* reply) {
  return Status::OK;
}

Status SyncServeice::Unary(ServerContext* context, const Complex* request,
                           Complex* reply) {
  reply->mutable_datas()->mutable_data1()->resize(
      request->numbers().number1());
  auto t0 = std::chrono::high_resolution_clock::now();
  while (true) {
    auto t1 = std::chrono::high_resolution_clock::now();
    size_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    if (ns > 100000) break;
  }
  // char thread_name[50];
  // pthread_getname_np(pthread_self(), thread_name, 50);
  // printf("%s, %lld is processing sync service: Unary\n", thread_name,
  //         std::this_thread::get_id());
  unary_request_total_size += request->datas().data1().length();
  unary_reply_total_size += reply->datas().data1().length();
  // if (++unary_batch_size == 100000) {
  //   printf("unary total request size = %lld, total reply size = %lld\n",
  //           unary_request_total_size, unary_reply_total_size);
  // }
  return Status::OK;
}

Status SyncServeice::ClientStream(ServerContext* context, ServerReader<Complex>* reader,
                                  Complex* reply) {
  Complex request;
  size_t total_data_size = 0;
  for (size_t id = 0; reader->Read(&request); id++) {
    // if (id % 1000 == 0) printf("ClientStream: %d-th read done\n", id);
    total_data_size += request.datas().data1().length();
  }
  reply->mutable_numbers()->set_number1(total_data_size);
  return Status::OK;
}

Status SyncServeice::ServerStream(ServerContext* context, const Complex* request,
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

Status SyncServeice::BiStream(ServerContext* context,
                              ServerReaderWriter<Complex, Complex>* stream) {
  Complex request, reply;
  for (size_t id = 0; stream->Read(&request); id++) {
    // if (id % 1000 == 0) printf("BiStream: %d-th read done\n", id);
    // std::unique_lock<std::mutex> lock(mu_);
    reply.mutable_numbers()->set_number1(request.datas().data1().length());
    reply.mutable_datas()->mutable_data1()->resize(
        request.numbers().number1());
    stream->Write(reply);
    // if (id % 1000 == 0) printf("BiStream: %d-th write done\n", id);
  }
  return Status::OK;
}

















