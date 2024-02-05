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

#include <numa.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/stats_time.h>
#include <numacompat1.h>
#include <csignal>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(uint32_t, threads, 1, "Number of threads");
ABSL_FLAG(uint32_t, cqs, 1, "Number of CQs");
ABSL_FLAG(uint32_t, resp, 1, "Reply size in bytes");
ABSL_FLAG(bool, numa, false, "Whether bind thread to NUMA node");
ABSL_FLAG(bool, profiling, false, "Enable Profiling");

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class ServerImpl final {
 public:
  ~ServerImpl() {
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    for (auto& cq : cqs_) {
      cq->Shutdown();
    }
    running_ = false;
  }

  // There is no shutdown handling in this code.
  void Run(uint16_t port, uint32_t n_cqs, uint32_t n_threads, uint32_t numa,
           bool profiling) {
    std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&service_);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.

    if (n_cqs > n_threads) {
      std::cerr << "Thread number is less than CQs" << std::endl;
      exit(1);
    }

    for (uint32_t i = 0; i < n_cqs; i++) {
      cqs_.emplace_back(builder.AddCompletionQueue());
    }

    finished_rpcs_.resize(n_threads, 0);

    // Finally assemble the server.
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << " CQs: " << n_cqs
              << " Threads: " << n_threads << " NUMA: " << numa << std::endl;

    running_ = true;
    curr_cq_ = 0;
    // Proceed to the server's main loop.
    for (uint32_t tid = 0; tid < n_threads; tid++) {
      ths_.push_back(std::thread([this, tid, numa, n_threads, profiling]() {
        if (numa > 0) {
          if (numa_available() >= 0) {
            auto nodes = numa_max_node() + 1;

            nodemask_t mask;

            nodemask_zero(&mask);
            nodemask_set(&mask, tid % nodes);
            numa_bind(&mask);
          } else {
            printf("NUMA is not available\n");
            exit(1);
          }
        }

        pthread_setname_np(pthread_self(),
                           ("work_th" + std::to_string(tid)).c_str());
        if (profiling) {
          grpc_stats_time_init(tid);
        }

        HandleRpcs(tid);
      }));
    }
    for (auto& th : ths_) {
      th.join();
    }
  }

  void PrintStatistics() {
    for (int i = 0; i < finished_rpcs_.size(); i++) {
      printf("Thread %d, RPCs %u\n", i, finished_rpcs_[i]);
    }
  }

 private:
  // Class encompasing the state and logic needed to serve a request.
  class CallData {
   public:
    // Let's implement a tiny state machine with the following states.
    enum CallStatus { CREATE, PROCESS, FINISH };
    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    CallData(Greeter::AsyncService* service, ServerCompletionQueue* cq)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {
      // Invoke the serving logic right away.
      Proceed();
    }

    void Proceed() {
      if (status_ == CREATE) {
        // Make this instance progress to the PROCESS state.
        status_ = PROCESS;

        // As part of the initial CREATE state, we *request* that the system
        // start processing SayHello requests. In this request, "this" acts are
        // the tag uniquely identifying the request (so that different CallData
        // instances can serve different requests concurrently), in this case
        // the memory address of this CallData instance.
        service_->RequestSayHello(&ctx_, &request_, &responder_, cq_, cq_,
                                  this);
      } else if (status_ == PROCESS) {
        // The actual processing.
        reply_.mutable_message()->resize(absl::GetFlag(FLAGS_resp));

        // And we are done! Let the gRPC runtime know we've finished, using the
        // memory address of this instance as the uniquely identifying tag for
        // the event.
        status_ = FINISH;
        responder_.Finish(reply_, Status::OK, this);
      } else {
        GPR_ASSERT(status_ == FINISH);
        // Once in the FINISH state, deallocate ourselves (CallData).
        delete this;
      }
    }

    CallStatus get_status() const { return status_; }

   private:
    // The means of communication with the gRPC runtime for an asynchronous
    // server.
    Greeter::AsyncService* service_;
    // The producer-consumer queue where for asynchronous server notifications.
    ServerCompletionQueue* cq_;
    // Context for the rpc, allowing to tweak aspects of it such as the use
    // of compression, authentication, as well as to send metadata back to the
    // client.
    ServerContext ctx_;

    // What we get from the client.
    HelloRequest request_;
    // What we send back to the client.
    HelloReply reply_;

    // The means to get back to the client.
    ServerAsyncResponseWriter<HelloReply> responder_;

    CallStatus status_;  // The current serving state.
  };

  // This can be run in multiple threads if needed.
  void HandleRpcs(int tid) {
    auto* cq = cqs_[tid % cqs_.size()].get();

    // Spawn a new CallData instance to serve new clients.
    new CallData(&service_, cq);
    void* tag;  // uniquely identifies a request.
    bool ok;
    while (running_) {
      // Block waiting to read the next event from the completion queue. The
      // event is uniquely identified by its tag, which in this case is the
      // memory address of a CallData instance.
      // The return value of Next should always be checked. This return value
      // tells us whether there is any kind of event or cq_ is shutting down.
      GPR_ASSERT(cq->Next(&tag, &ok));
      GPR_ASSERT(ok);
      auto* call = static_cast<CallData*>(tag);

      if (call->get_status() == CallData::PROCESS) {
        finished_rpcs_[tid]++;
        if (finished_rpcs_[tid] == 1000) {
          grpc_stats_time_enable();
        }
        // Spawn a new CallData instance to serve new clients while we process
        // the one for this CallData. The instance will deallocate itself as
        // part of its FINISH state.
        new CallData(&service_, cq);
      }
      call->Proceed();
    }
  }

  void HandleRpcsAsync(int tid) {
    gpr_timespec deadline;
    deadline.clock_type = GPR_TIMESPAN;
    deadline.tv_sec = 0;
    deadline.tv_nsec = 0;
    auto* cq = cqs_[tid % cqs_.size()].get();

    new CallData(&service_, cq);

    while (running_) {
      cq = cqs_[curr_cq_++ % cqs_.size()].get();
      void* tag;  // uniquely identifies a request.
      bool ok;
      auto ev = cq->AsyncNext(&tag, &ok, deadline);

      if (ev == grpc::CompletionQueue::NextStatus::GOT_EVENT) {
        auto* call = static_cast<CallData*>(tag);
        GPR_ASSERT(ok);
        if (call->get_status() == CallData::PROCESS) {
          // Spawn a new CallData instance to serve new clients while we process
          // the one for this CallData. The instance will deallocate itself as
          // part of its FINISH state.
          cq = cqs_[curr_cq_++ % cqs_.size()].get();
          new CallData(&service_, cq);
        }
        call->Proceed();
      }
    }
  }

  std::vector<std::unique_ptr<ServerCompletionQueue>> cqs_;
  std::vector<uint32_t> finished_rpcs_;
  std::atomic_uint32_t curr_cq_;
  std::vector<std::thread> ths_;
  Greeter::AsyncService service_;
  std::unique_ptr<Server> server_;
  std::atomic_bool running_;
};

ServerImpl* p_server;

void sig_handler(int signum) {
  grpc_stats_time_disable();
  grpc_stats_time_print();
  p_server->PrintStatistics();

  if (signum == SIGINT) {
    exit(0);
  }
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ServerImpl server;

  if (absl::GetFlag(FLAGS_profiling)) {
    p_server = &server;
    signal(SIGUSR1, sig_handler);
    signal(SIGINT, sig_handler);
  }

  server.Run(absl::GetFlag(FLAGS_port), absl::GetFlag(FLAGS_cqs),
             absl::GetFlag(FLAGS_threads), absl::GetFlag(FLAGS_numa),
             absl::GetFlag(FLAGS_profiling));

  return 0;
}
