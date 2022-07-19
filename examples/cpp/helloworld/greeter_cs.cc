/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "flags.h"
#include "gflags/gflags.h"
using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloReply1;
using helloworld::HelloReply2;
using helloworld::HelloReply3;
using helloworld::HelloRequest;
using helloworld::HelloRequest1;
using helloworld::HelloRequest2;
using helloworld::HelloRequest3;

std::string gen_random(const int len) {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::string tmp_s;
  tmp_s.resize(len, 'a');

  //  for (int i = 0; i < len; ++i) {
  //    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  //  }

  return tmp_s;
}

class GreeterClient {
 public:
  explicit GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  void CheckStatus(Status status, const char* extra) {
    if (!status.ok()) {
      gpr_log(GPR_ERROR, "status is not ok, msg: %s, detail: %s, extra: %s",
              status.error_message().c_str(), status.error_details().c_str(),
              extra);
    }
  }

  std::string SayHello(CompletionQueue& cq, const std::string& user) {
    HelloRequest request;
    request.set_name(user);
    HelloReply reply;
    ClientContext context;
    Status status;
    context.set_wait_for_ready(true);

    std::unique_ptr<ClientAsyncResponseReader<HelloReply>> rpc(
        stub_->PrepareAsyncSayHello(&context, request, &cq));

    rpc->StartCall();
    rpc->Finish(&reply, &status, (void*)1);
    void* got_tag;
    bool ok = false;
    GPR_ASSERT(cq.Next(&got_tag, &ok));
    GPR_ASSERT(got_tag == (void*)1);
    GPR_ASSERT(ok);

    CheckStatus(status, "SayHello");
    GPR_ASSERT(status.ok());
    return reply.message();
  }

  std::string SayHello1(CompletionQueue& cq, const std::string& user) {
    HelloRequest1 request;
    request.set_name(user);
    HelloReply1 reply;
    ClientContext context;
    Status status;
    context.set_wait_for_ready(true);

    std::unique_ptr<ClientAsyncResponseReader<HelloReply1>> rpc(
        stub_->PrepareAsyncSayHello1(&context, request, &cq));

    rpc->StartCall();
    rpc->Finish(&reply, &status, (void*)1);
    void* got_tag;
    bool ok = false;
    GPR_ASSERT(cq.Next(&got_tag, &ok));
    GPR_ASSERT(got_tag == (void*)1);
    GPR_ASSERT(ok);
    CheckStatus(status, "SayHello1");
    GPR_ASSERT(status.ok());
    return reply.message();
  }

  std::string SayHello2(CompletionQueue& cq, const std::string& user) {
    HelloRequest2 request;
    request.set_name(user);
    HelloReply2 reply;
    ClientContext context;
    Status status;
    context.set_wait_for_ready(true);

    std::unique_ptr<ClientAsyncResponseReader<HelloReply2>> rpc(
        stub_->PrepareAsyncSayHello2(&context, request, &cq));

    rpc->StartCall();
    rpc->Finish(&reply, &status, (void*)1);
    void* got_tag;
    bool ok = false;
    GPR_ASSERT(cq.Next(&got_tag, &ok));
    GPR_ASSERT(got_tag == (void*)1);
    GPR_ASSERT(ok);
    CheckStatus(status, "SayHello2");
    GPR_ASSERT(status.ok());
    return reply.message();
  }

  std::string SayHello3(CompletionQueue& cq, const std::string& user) {
    HelloRequest3 request;
    request.set_name(user);
    HelloReply3 reply;
    ClientContext context;
    Status status;
    context.set_wait_for_ready(true);

    std::unique_ptr<ClientAsyncResponseReader<HelloReply3>> rpc(
        stub_->PrepareAsyncSayHello3(&context, request, &cq));

    rpc->StartCall();
    rpc->Finish(&reply, &status, (void*)1);
    void* got_tag;
    bool ok = false;
    GPR_ASSERT(cq.Next(&got_tag, &ok));
    GPR_ASSERT(got_tag == (void*)1);
    GPR_ASSERT(ok);
    CheckStatus(status, "SayHello3");
    GPR_ASSERT(status.ok());
    return reply.message();
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

enum CallStatus { CREATE, PROCESS, FINISH };

class CallBase {
 public:
  CallBase(Greeter::AsyncService* service, ServerCompletionQueue* cq)
      : service_(service), cq_(cq), status_(CREATE) {}

  virtual ~CallBase() = default;
  virtual void Proceed() = 0;
  ServerContext ctx_;
  Greeter::AsyncService* service_;
  ServerCompletionQueue* cq_;
  CallStatus status_;
};

class CallData : public CallBase {
 public:
  CallData(Greeter::AsyncService* service, ServerCompletionQueue* cq)
      : CallBase(service, cq), responder_(&ctx_) {
    Proceed();
  }

  void Proceed() {
    if (status_ == CREATE) {
      status_ = PROCESS;
      service_->RequestSayHello(&ctx_, &request_, &responder_, cq_, cq_, this);
    } else if (status_ == PROCESS) {
      new CallData(service_, cq_);

      //        std::string prefix("Hello ");
      reply_.mutable_message()->assign(gen_random(rand() % FLAGS_resp));
      status_ = FINISH;
      responder_.Finish(reply_, Status::OK, this);
    } else {
      GPR_ASSERT(status_ == FINISH);
      delete this;
    }
  }

 private:
  HelloRequest request_;
  HelloReply reply_;
  ServerAsyncResponseWriter<HelloReply> responder_;
};

class CallData1 : public CallBase {
 public:
  CallData1(Greeter::AsyncService* service, ServerCompletionQueue* cq)
      : CallBase(service, cq), responder_(&ctx_) {
    Proceed();
  }

  void Proceed() {
    if (status_ == CREATE) {
      status_ = PROCESS;
      service_->RequestSayHello1(&ctx_, &request_, &responder_, cq_, cq_, this);
    } else if (status_ == PROCESS) {
      new CallData1(service_, cq_);

      reply_.mutable_message()->assign(gen_random(rand() % FLAGS_resp));
      status_ = FINISH;
      responder_.Finish(reply_, Status::OK, this);
    } else {
      GPR_ASSERT(status_ == FINISH);
      delete this;
    }
  }

 private:
  HelloRequest1 request_;
  HelloReply1 reply_;
  ServerAsyncResponseWriter<HelloReply1> responder_;
};

class CallData2 : public CallBase {
 public:
  CallData2(Greeter::AsyncService* service, ServerCompletionQueue* cq)
      : CallBase(service, cq), responder_(&ctx_) {
    Proceed();
  }

  void Proceed() {
    if (status_ == CREATE) {
      status_ = PROCESS;
      service_->RequestSayHello2(&ctx_, &request_, &responder_, cq_, cq_, this);
    } else if (status_ == PROCESS) {
      new CallData2(service_, cq_);

      //        std::string prefix("Hello ");
      reply_.mutable_message()->assign(gen_random(rand() % FLAGS_resp));
      status_ = FINISH;
      responder_.Finish(reply_, Status::OK, this);
    } else {
      GPR_ASSERT(status_ == FINISH);
      delete this;
    }
  }

 private:
  HelloRequest2 request_;
  HelloReply2 reply_;
  ServerAsyncResponseWriter<HelloReply2> responder_;
};

class CallData3 : public CallBase {
 public:
  CallData3(Greeter::AsyncService* service, ServerCompletionQueue* cq)
      : CallBase(service, cq), responder_(&ctx_) {
    Proceed();
  }

  void Proceed() {
    if (status_ == CREATE) {
      status_ = PROCESS;
      service_->RequestSayHello3(&ctx_, &request_, &responder_, cq_, cq_, this);
    } else if (status_ == PROCESS) {
      new CallData3(service_, cq_);

      //        std::string prefix("Hello ");
      reply_.mutable_message()->assign(gen_random(rand() % FLAGS_resp));
      status_ = FINISH;
      responder_.Finish(reply_, Status::OK, this);
    } else {
      GPR_ASSERT(status_ == FINISH);
      delete this;
    }
  }

 private:
  HelloRequest3 request_;
  HelloReply3 reply_;
  ServerAsyncResponseWriter<HelloReply3> responder_;
};

class ServerImpl final {
 public:
  ~ServerImpl() {
    server_->Shutdown();
    for (auto& cq : cqs_) {
      cq->Shutdown();
    }
  }

  void Run() {
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    builder.SetOption(
        grpc::MakeChannelArgumentOption(GRPC_ARG_ALLOW_REUSEPORT, 0));
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    for (int i = 0; i < FLAGS_threads; i++) {
      cqs_.emplace_back(builder.AddCompletionQueue());
    }
    builder.SetMaxReceiveMessageSize(-1);
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << std::endl;

    HandleRpcs();
  }

 private:
  void HandleRpcs() {
    std::vector<std::thread> ths;

    for (int i = 0; i < cqs_.size(); i++) {
      ths.emplace_back(
          [this](int idx) {
            pthread_setname_np(pthread_self(),
                               ("server_thread" + std::to_string(idx)).c_str());
            auto& cq = cqs_[idx];
            new CallData(&service_, cq.get());
            new CallData1(&service_, cq.get());
            new CallData2(&service_, cq.get());
            new CallData3(&service_, cq.get());
            void* tag;
            bool ok;
            while (true) {
              GPR_ASSERT(cq->Next(&tag, &ok));
              GPR_ASSERT(ok);
              static_cast<CallBase*>(tag)->Proceed();
            }
          },
          i);
    }

    for (auto& th : ths) {
      th.join();
    }
  }

  std::vector<std::unique_ptr<ServerCompletionQueue>> cqs_;
  Greeter::AsyncService service_;
  std::unique_ptr<Server> server_;
};

int main(int argc, char** argv) {
  ServerImpl server;

  gflags::SetUsageMessage("Usage: mpiexec [mpi_opts] ./main [main_opts]");
  if (argc == 1) {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "main");
    exit(1);
  }
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(-1);

  GreeterClient greeter(grpc::CreateCustomChannel(
      FLAGS_host + ":50051", grpc::InsecureChannelCredentials(), args));

  int rpc_count = FLAGS_batch;
  std::vector<std::thread> ths;

  ths.emplace_back([rpc_count, &greeter]() {
    CompletionQueue cq;
    for (int i = 0; i < rpc_count; i++) {
      std::string user = gen_random(rand() % FLAGS_req);

      greeter.SayHello(cq, user);
    }
    std::cout << "Exit " << getpid() << std::endl;
  });
  ths.emplace_back([rpc_count, &greeter]() {
    CompletionQueue cq;
    for (int i = 0; i < rpc_count; i++) {
      std::string user = gen_random(rand() % FLAGS_req);

      greeter.SayHello1(cq, user);
    }
    std::cout << "Exit1 " << getpid() << std::endl;
  });
  ths.emplace_back([rpc_count, &greeter]() {
    CompletionQueue cq;
    for (int i = 0; i < rpc_count; i++) {
      std::string user = gen_random(rand() % FLAGS_req);

      greeter.SayHello2(cq, user);
    }
    std::cout << "Exit2 " << getpid() << std::endl;
  });
  ths.emplace_back([rpc_count, &greeter]() {
    CompletionQueue cq;
    for (int i = 0; i < rpc_count; i++) {
      std::string user = gen_random(rand() % FLAGS_req);

      greeter.SayHello3(cq, user);
    }
    std::cout << "Exit3 " << getpid() << std::endl;
  });
  server.Run();

  return 0;
}
