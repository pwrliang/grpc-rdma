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

#include <grpc/support/log.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/grpcpp.h>
#include <signal.h>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include "bytebuffer_util.h"
#include "proc_parser.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "flags.h"
#include "gflags/gflags.h"
#include "grpcpp/stats_time.h"
#include "stopwatch.h"

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;
using helloworld::RecvBufRequest;
using helloworld::RecvBufRespExtra;
using helloworld::RecvBufResponse;

std::map<std::string, cpu_time_t> cpu_time1;

void* tag(int i) { return reinterpret_cast<void*>(i); }

class ServerImpl final {
 public:
  ~ServerImpl() {
    server_->Shutdown();
    for (auto& cq : cqs_) {
      cq->Shutdown();
    }
  }

  void StartService() {
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    builder.SetOption(
        grpc::MakeChannelArgumentOption(GRPC_ARG_ALLOW_REUSEPORT, 0));
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    builder.SetMaxReceiveMessageSize(-1);
    for (int i = 0; i < FLAGS_cqs; i++) {
      cqs_.emplace_back(builder.AddCompletionQueue());
    }
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address
              << ", thread: " << FLAGS_threads << ", cq: " << FLAGS_cqs
              << std::endl;
  }

  void StartGenericService() {
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    builder.SetOption(
        grpc::MakeChannelArgumentOption(GRPC_ARG_ALLOW_REUSEPORT, 0));
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterAsyncGenericService(&generic_service_);
    builder.SetMaxReceiveMessageSize(-1);
    for (int i = 0; i < FLAGS_cqs; i++) {
      cqs_.emplace_back(builder.AddCompletionQueue());
    }
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address
              << ", thread: " << FLAGS_threads << ", cq: " << FLAGS_cqs
              << std::endl;
  }

  class CallData {
   public:
    CallData(HelloReply& reply, Greeter::AsyncService* service,
             ServerCompletionQueue* cq)
        : reply_(reply),
          service_(service),
          cq_(cq),
          responder_(&ctx_),
          status_(CREATE) {
      Proceed();
    }

    void Proceed() {
      if (status_ == CREATE) {
        cycles_t c1 = get_cycles();
        status_ = PROCESS;
        service_->RequestSayHello(&ctx_, &request_, &responder_, cq_, cq_,
                                  this);
        cycles_t c2 = get_cycles();
        grpc_stats_time_add(GRPC_STATS_TIME_SERVER_RPC_REQUEST, c2 - c1, 1);
      } else if (status_ == PROCESS) {
        new CallData(reply_, service_, cq_);

        if (request_.has_start_benchmark() && request_.start_benchmark()) {
          cpu_time1 = get_cpu_time_per_core();
          grpc_stats_time_enable();
        }

        if (request_.name() == "fin") {
          auto cpu_time2 = get_cpu_time_per_core();

          int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
          auto time_diff = cpu_time2["cpu"] - cpu_time1["cpu"];
          printf(
              "[S] CPU-TIME (s) user: %lf idle: %lf nice: %lf sys: %lf "
              "iowait: %lf "
              "irq: %lf softirq: %lf sum: %lf\n",
              time_diff.t_user, time_diff.t_idle, time_diff.t_nice,
              time_diff.t_system, time_diff.t_iowait, time_diff.t_irq,
              time_diff.t_softirq, time_diff.t_sum);
          for (int i = 0; i < num_cores; i++) {
            auto cpu_name = "cpu" + std::to_string(i);
            time_diff = cpu_time2[cpu_name] - cpu_time1[cpu_name];
            printf(
                "[S] CPU%d-TIME (s) user: %lf idle: %lf nice: %lf sys: %lf "
                "iowait: %lf "
                "irq: %lf softirq: %lf sum: %lf\n",
                i, time_diff.t_user, time_diff.t_idle, time_diff.t_nice,
                time_diff.t_system, time_diff.t_iowait, time_diff.t_irq,
                time_diff.t_softirq, time_diff.t_sum);
          }
        }

        status_ = FINISH;
        cycles_t c1 = get_cycles();
        responder_.Finish(reply_, Status::OK, this);
        cycles_t c2 = get_cycles();
        grpc_stats_time_add(GRPC_STATS_TIME_SERVER_RPC_FINISH, c2 - c1, 1);
      } else {
        GPR_ASSERT(status_ == FINISH);
        delete this;
      }
    }

   private:
    Greeter::AsyncService* service_;
    ServerCompletionQueue* cq_;
    ServerContext ctx_;
    HelloRequest request_;
    HelloReply& reply_;
    ServerAsyncResponseWriter<HelloReply> responder_;
    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status_;
  };

  class GenericCallData {
   public:
    GenericCallData(grpc::AsyncGenericService* service,
                    ServerCompletionQueue* cq)
        : service_(service),
          stream_(grpc::GenericServerAsyncReaderWriter(&ctx_)),
          cq_(cq),
          status_(CREATE) {
      Proceed();
    }

    ~GenericCallData() {}

    void Proceed() {
      if (status_ == CREATE) {
        service_->RequestCall(&ctx_, &stream_, cq_, cq_, this);
        status_ = READ;
      } else if (status_ == READ) {
        stream_.Read(&req_, this);

        new GenericCallData(service_, cq_);

        status_ = WRITE;
      } else if (status_ == WRITE) {
        std::vector<grpc::Slice> slices;

        req_.Dump(&slices);
        // Server got a notification that we should start benchmark
        if (*slices[0].begin() == 0xab) {
          gpr_log(GPR_INFO, "Start benchmark\n");
          grpc_stats_time_enable();
        }

        grpc::WriteOptions options;

        // create response
        slices.clear();
        slices.push_back(grpc::Slice(FLAGS_resp));
        resp_ = grpc::ByteBuffer(slices.data(), slices.size());
        stream_.WriteAndFinish(resp_, options, Status::OK, this);
        status_ = FINISH;
      } else {
        GPR_ASSERT(status_ == FINISH);
        delete this;
      }
    }

   private:
    grpc::AsyncGenericService* service_;
    ServerCompletionQueue* cq_;
    grpc::GenericServerContext ctx_;
    grpc::ByteBuffer req_, resp_;

    grpc::GenericServerAsyncReaderWriter stream_;
    enum CallStatus { CREATE, READ, WRITE, FINISH };
    CallStatus status_;
  };

  void HandleRpcs() {
    std::vector<std::thread> ths;

    for (int i = 0; i < FLAGS_threads; i++) {
      ths.emplace_back(
          [this](int idx) {
            HelloReply reply;
            reply.mutable_message()->resize(FLAGS_resp);
            auto& cq = cqs_[idx % cqs_.size()];
            new CallData(reply, &service_, cq.get());
            void* tag;
            bool ok;

            pthread_setname_np(pthread_self(),
                               ("work_th" + std::to_string(idx)).c_str());
            grpc_stats_time_init();

            while (true) {
              cycles_t c1 = get_cycles();
              GPR_ASSERT(cq->Next(&tag, &ok));
              GPR_ASSERT(ok);
              cycles_t c2 = get_cycles();
              static_cast<CallData*>(tag)->Proceed();
              grpc_stats_time_add(GRPC_STATS_TIME_SERVER_CQ_NEXT, c2 - c1);
            }
          },
          i);
    }

    for (auto& th : ths) {
      th.join();
    }
  }

  void HandleGenericRpcs() {
    std::vector<std::thread> ths;

    for (int i = 0; i < FLAGS_threads; i++) {
      ths.emplace_back(
          [this](int idx) {
            auto& cq = cqs_[idx % cqs_.size()];
            new GenericCallData(&generic_service_, cq.get());
            void* tag;
            bool ok;

            pthread_setname_np(pthread_self(),
                               ("work_th" + std::to_string(idx)).c_str());
            grpc_stats_time_init();

            while (true) {
              cycles_t c1 = get_cycles();
              GPR_ASSERT(cq->Next(&tag, &ok));
              GPR_ASSERT(ok);
              cycles_t c2 = get_cycles();
              static_cast<GenericCallData*>(tag)->Proceed();
              grpc_stats_time_add(GRPC_STATS_TIME_SERVER_CQ_NEXT, c2 - c1);
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
  grpc::AsyncGenericService generic_service_;
  std::unique_ptr<Server> server_;
};

void grpc_stats_time_print();

void sig_handler(int signum) {
  grpc_stats_time_print();

  if (signum == SIGINT) {
    exit(0);
  }
}

int main(int argc, char** argv) {
  signal(SIGUSR1, sig_handler);
  signal(SIGINT, sig_handler);

  gflags::SetUsageMessage("Usage: mpiexec [mpi_opts] ./main [main_opts]");
  if (argc == 1) {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "main");
    exit(1);
  }
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (!FLAGS_mode.empty()) {
    setenv("GRPC_PLATFORM_TYPE", FLAGS_mode.c_str(), 1);
  }
  if (FLAGS_executor > 0) {
    setenv("GRPC_EXECUTOR", std::to_string(FLAGS_executor).c_str(), 1);
  }
  ServerImpl server;

  if (FLAGS_generic) {
    server.StartGenericService();
    server.HandleGenericRpcs();
  } else {
    server.StartService();
    server.HandleRpcs();
  }

  gflags::ShutDownCommandLineFlags();

  return 0;
}
