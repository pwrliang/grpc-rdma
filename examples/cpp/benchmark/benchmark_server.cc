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
#include "benchmark_utils.h"

#ifdef BAZEL_BUILD
#include "examples/protos/benchamrk.grpc.pb.h"
#else

#include "benchmark.grpc.pb.h"

#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::CompletionQueue;
using grpc::ServerCompletionQueue;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerAsyncReader;
using grpc::ServerAsyncWriter;
using grpc::ServerAsyncReaderWriter;
using grpc::Status;
using benchmark::BENCHMARK;
using benchmark::Data_Empty;
using benchmark::Data_Bytes;
using benchmark::Data_Int64;
using benchmark::Data_String;
using benchmark::Complex;

class SyncServeice final : public BENCHMARK::Service {
public:

    Status SayHello(ServerContext *context, const Data_Empty *request, Data_Empty *reply) override {
        return Status::OK;
    }

    Status BiUnary(ServerContext* context, const Complex* request, Complex* reply) override {
      reply->mutable_datas()->mutable_data1()->resize(request->numbers().number1());
      char thread_name[50];
      pthread_getname_np(pthread_self(), thread_name, 50);
      printf("%s, %lld is processing sync service: BiUnary\n", 
              thread_name, std::this_thread::get_id());
      return Status::OK;
    }

    Status ClientStream(ServerContext* context, ServerReader<Complex>* reader, Complex* reply) override {
      Complex request;
      size_t total_data_size = 0;
      for (size_t id = 0; reader->Read(&request); id++) {
        if (id % 1000 == 0) printf("ClientStream: %d-th read done\n", id);
        total_data_size += request.datas().data1().length();
      }
      reply->mutable_numbers()->set_number1(total_data_size);
      return Status::OK;
    }

    Status ServerStream(ServerContext *context, const Complex *request, ServerWriter<Complex> *writer) override {
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

    Status BiStream(ServerContext* context, ServerReaderWriter<Complex, Complex>* stream) override {
      Complex request, reply;
      for (size_t id = 0; stream->Read(&request); id++) {
        if (id % 1000 == 0) printf("BiStream: %d-th read done\n", id);
        std::unique_lock<std::mutex> lock(mu_);
        reply.mutable_numbers()->set_number1(request.datas().data1().length());
        reply.mutable_datas()->mutable_data1()->resize(request.numbers().number1());
        stream->Write(reply);
        if (id % 1000 == 0) printf("BiStream: %d-th write done\n", id);
      }
      return Status::OK;
    }

    ::grpc::Status Test(::grpc::ServerContext *context, const ::benchmark::TestReq *request,
                        ::benchmark::TestResp *response) override {
        *response->mutable_data() = request->data();
        return grpc::Status::OK;
    }

private:
    std::mutex mu_;
};

class BenchmarkServer {
  public:
    enum ServiceType {SAYHELLO, BIUNARY, CLIENTSTREAM, SERVERSTREAM, BISTREAM};

    BenchmarkServer(const std::string server_address, 
                    bool sync_enable, size_t sync_thread_num,
                    bool async_enable, size_t async_threads_num, size_t async_cqs_num)
      : sync_enable_(sync_enable), sync_threads_num_(sync_thread_num),
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
      printf("Server: sync enable %d, sync threads num %d; async enable %d, async threads num %d, async threads num %d\n",
             sync_enable, sync_thread_num,
             async_enable, async_threads_num, async_cqs_num);
      printf("Server is listening on %s\n", server_address.c_str());
    }

    void SyncRun() {
      server_->Wait();
    }

    static void SyncThreadRunThis(BenchmarkServer* server, int thread_id) {
      std::string thread_name = "thread " + std::to_string(thread_id);
      pthread_setname_np(pthread_self(), thread_name.c_str());
      char _thread_name_[20];
      pthread_getname_np(pthread_self(), _thread_name_, 20);
      printf("%s, %lld is waitting\n", 
              _thread_name_, std::this_thread::get_id());
      server->SyncRun();
    }

    void AsyncHandleAll(ServerCompletionQueue* notification_cq, CompletionQueue* call_cq, int thread_id) {
      new CallData(&async_service_, call_cq, notification_cq, thread_id, SAYHELLO);
      new CallData(&async_service_, call_cq, notification_cq, thread_id, BIUNARY);

      auto deadline = gpr_time_from_millis(10, GPR_TIMESPAN);
      void* tag = nullptr;
      bool ok;
      grpc::CompletionQueue::NextStatus status;
      // while ((status = notification_cq->AsyncNext(&tag, &ok, deadline)) != grpc::CompletionQueue::SHUTDOWN) {

      // }
      bool alive = true;
      while (alive) {
        switch (notification_cq->AsyncNext(&tag, &ok, deadline)) {
          case grpc::CompletionQueue::SHUTDOWN:
            alive = false;
            break;
          // case grpc::CompletionQueue::TIMEOUT:
          //   continue;
          case grpc::CompletionQueue::GOT_EVENT:
            if (ok) {
              static_cast<CallData*>(tag)->Proceed();
            } else {
              printf("not ok\n");
              exit(-1);
            }
        }
      }
    }

    static void AsyncThreadRunThis(BenchmarkServer* server, int thread_id) {

    }

    void Run() {
      // if (async_enable_) {

      // }
      if (sync_enable_) {
        // for (size_t i = 0; i < sync_threads_num_; i++) {
        //   sync_threads_.emplace_back(std::thread(SyncThreadRunThis, this, i));
        // }
        SyncThreadRunThis(this, 0);
      }
      if (async_enable_) {
        for (size_t i = 0; i < async_threads_num_; i++) async_threads_[i].join();
      }
      // if (sync_enable_) {
      //   for (size_t i = 0; i < sync_threads_num_; i++) sync_threads_[i].join();
      // }
    }

  private:
    class CallData {
      public:
        enum CallStatus { CREATE, PROCESS, FINISH };
        typedef struct SayHelloPackage {
          SayHelloPackage(ServerContext* ctx) : responder(ctx) {}
          Data_Empty request;
          Data_Empty reply;
          ServerAsyncResponseWriter<Data_Empty> responder;
        } SayHelloPackage;
        typedef struct BiUnaryPackage {
          BiUnaryPackage(ServerContext* ctx) : responder(ctx) {}
          Complex request;
          Complex reply;
          ServerAsyncResponseWriter<Complex> responder;
        } BiUnaryPackage;
        typedef struct ClientStreamPackage {
          ClientStreamPackage(ServerContext* ctx) : responder(ctx) {}
          Complex request;
          Complex reply;
          ServerAsyncReader<Complex, Complex> responder;
        } ClientStreamPackage;
        typedef struct ServerStreamPackage {
          ServerStreamPackage(ServerContext* ctx) : responder(ctx) {}
          Complex request;
          Complex reply;
          ServerAsyncWriter<Complex> responder;
        } ServerStreamPackage;
        typedef struct BiStreamPackage {
          BiStreamPackage(ServerContext* ctx) : responder(ctx) {}
          Complex request;
          Complex reply;
          ServerAsyncReaderWriter<Complex, Complex> responder;
        } BiStreamPackage;

        CallData(BENCHMARK::AsyncService* service,
                 CompletionQueue* call_cq,
                 ServerCompletionQueue* notification_cq,
                 int thread_id,
                 ServiceType type)
                 : service_(service),
                   call_cq_(call_cq),
                   notification_cq_(notification_cq),
                   thread_id_(thread_id),
                   type_(type),
                   status_(CREATE) {
          switch (type) {
            case SAYHELLO:
              say_hello_pkg_ = new SayHelloPackage(&ctx_);
              break;
            case BIUNARY:
              bi_unary_pkg_ = new BiUnaryPackage(&ctx_);
              break;
            case CLIENTSTREAM:
              client_stream_pkg_ = new ClientStreamPackage(&ctx_);
              break;
            case SERVERSTREAM:
              server_stream_pkg_ = new ServerStreamPackage(&ctx_);
              break;
            case BISTREAM:
              bi_stream_pkg_ = new BiStreamPackage(&ctx_);
              break;
            default:
              printf("unknown service type\n");
              exit(-1);
          }
          Proceed();
        }

        void Proceed() {
          if (status_ == CREATE) {
            status_ = PROCESS;
            switch (type_) {
              case SAYHELLO:
                service_->RequestSayHello(&ctx_, &say_hello_pkg_->request, &say_hello_pkg_->responder, 
                                          call_cq_, notification_cq_, this);
                break;
              case BIUNARY:
                service_->RequestBiUnary(&ctx_, &bi_unary_pkg_->request, &bi_unary_pkg_->responder, 
                                         call_cq_, notification_cq_, this);
                break;
              case CLIENTSTREAM:
                service_->RequestClientStream(&ctx_, &client_stream_pkg_->responder, 
                                              call_cq_, notification_cq_, this);
                break;
              case SERVERSTREAM:
                service_->RequestServerStream(&ctx_, &server_stream_pkg_->request, &server_stream_pkg_->responder,
                                              call_cq_, notification_cq_, this);
                break;
              case BISTREAM:
                service_->RequestBiStream(&ctx_, &bi_stream_pkg_->responder,
                                          call_cq_, notification_cq_, this);
                break;
              default:
                printf("unknown service type\n");
                exit(-1);
            }
            return;
          }
          if (status_ == PROCESS) {
            new CallData(service_, call_cq_, notification_cq_, thread_id_, type_);
            switch (type_) {
              case SAYHELLO:
                ProcessSayHello();
                break;
              case BIUNARY:
                ProcessBiUnary();
                break;
              case CLIENTSTREAM:
                ProcessClientStream();
                break;
              case SERVERSTREAM:
                ProcessServerStream();
                break;
              case BISTREAM:
                ProcessBiStream();
                break;
              default:
                printf("unknown service type\n");
                exit(-1);
            }
            return;
          }
          if (status_ == FINISH) {
            delete this;
            return;
          }
          printf("unknown status\n");
          exit(-1);
        }

        void ProcessSayHello() {
          ServerAsyncResponseWriter<Data_Empty>& responder = say_hello_pkg_->responder;
          Data_Empty& request = say_hello_pkg_->request;
          Data_Empty& reply = say_hello_pkg_->reply;

          status_ = FINISH;
          responder.Finish(reply, Status::OK, this);
        }

        void ProcessBiUnary() {
          ServerAsyncResponseWriter<Complex>& responder = bi_unary_pkg_->responder;
          Complex& request = bi_unary_pkg_->request;
          Complex& reply = bi_unary_pkg_->reply;

          reply.mutable_datas()->mutable_data1()->resize(request.numbers().number1());
          status_ = FINISH;
          responder.Finish(reply, Status::OK, this);
        }

        void ProcessClientStream() {
          // ServerAsyncReader<Complex, Complex>& responder = client_stream_pkg_->responder;
          // Complex& request = client_stream_pkg_->request;
          // Complex& reply = client_stream_pkg_->reply;

          // size_t total_data_size = 0;
          // CallData* call_data = nullptr;
          // responder.Read(&request, call_data);
          // for (size_t id = 0; call_data && call_data == this; id++) {
          //   if (id % 1000 == 0) printf("ClientStream: %d-th read done\n", id);
          //   total_data_size += request.datas().data1().length();
          //   responder.Read(&request, call_data);
          // }
          // reply.mutable_numbers()->set_number1(total_data_size);
          // responder.Finish(reply, Status::OK, this);
        }

        void ProcessServerStream() {
          // ServerAsyncWriter<Complex>& responder = server_stream_pkg_->responder;
          // Complex& request = server_stream_pkg_->request;
          // Complex& reply = server_stream_pkg_->reply;

          // size_t batch_size = request.numbers().number1();
          // size_t min_request_size = request.numbers().number2();
          // size_t max_request_size = request.numbers().number3();
          // for (size_t i = 0; i < batch_size; i++) {
          //   size_t request_size = random(min_request_size, max_request_size);
          //   reply.mutable_datas()->mutable_data1()->resize(request_size);
          //   reply.mutable_numbers()->set_number1(min_request_size);
          //   reply.mutable_numbers()->set_number2(max_request_size);
          //   responder.Write(reply, this);
          // }
          // responder.Finish(Status::OK, this);
        }

        void ProcessBiStream() {
          // ServerAsyncReaderWriter<Complex, Complex>& responder = bi_stream_pkg_->responder;
          // Complex& request = bi_stream_pkg_->request;
          // Complex& reply = bi_stream_pkg_->reply;

          // CallData* call_data = nullptr;
          // responder.Read(&request, call_data);
          // for (size_t id = 0; call_data && call_data == this; id++) {
          //   if (id % 1000 == 0) printf("BiStream: %d-th read done\n", id);
          //   std::unique_lock<std::mutex> lock(mu_);
          //   reply.mutable_numbers()->set_number1(request.datas().data1().length());
          //   reply.mutable_datas()->mutable_data1()->resize(request.numbers().number1());
          //   responder.Write(reply, this);
          //   if (id % 1000 == 0) printf("BiStream: %d-th write done\n", id);
          //   responder.Read(&request, call_data);
          // }
          // responder.Finish(Status::OK, this);
        }
 
      protected:
        BENCHMARK::AsyncService* service_;
        CompletionQueue* call_cq_ = nullptr;
        ServerCompletionQueue* notification_cq_ = nullptr;
        ServerContext ctx_;
        int thread_id_;
        ServiceType type_;
        CallStatus status_;

        SayHelloPackage* say_hello_pkg_ = nullptr;
        BiUnaryPackage* bi_unary_pkg_ = nullptr;
        ClientStreamPackage* client_stream_pkg_ = nullptr;
        ServerStreamPackage* server_stream_pkg_ = nullptr;
        BiStreamPackage* bi_stream_pkg_ = nullptr;
    };


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

DEFINE_string(server_address, "0.0.0.0:50051", "");
DEFINE_bool(sync_enable, true, "");
DEFINE_int32(sync_thread_num, 1, "");
DEFINE_bool(async_enable, false, "");
DEFINE_int32(async_cq_num, 0, "");
DEFINE_int32(async_thread_num, 0, "");
DEFINE_string(platform, "TCP", "which transport protocol used");
DEFINE_string(verbosity, "ERROR", "");

int main(int argc, char **argv) {
    ::gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::gflags::ShutDownCommandLineFlags();

  const std::string server_address = FLAGS_server_address;
  const bool sync_enable = FLAGS_sync_enable;
  const int sync_thread_num = FLAGS_sync_thread_num;
  const bool async_enable = FLAGS_async_enable;
  const int async_cq_num = FLAGS_async_cq_num;
  const int async_thread_num = FLAGS_async_thread_num;
  const std::string platform = FLAGS_platform;
  const std::string verbosity = FLAGS_verbosity;

    setenv("GRPC_PLATFORM_TYPE", platform.c_str(), 1);
    setenv("RDMA_VERBOSITY", verbosity.c_str(), 1);

  BenchmarkServer server(server_address,
                         sync_enable, sync_thread_num,
                         async_enable, async_thread_num, async_cq_num);

    server.Run();

    return 0;
}