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
#include <numacompat1.h>

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "micro_benchmark.grpc.pb.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(uint32_t, threads, 1, "Number of threads");
ABSL_FLAG(uint32_t, cqs, 1, "Number of CQs");
ABSL_FLAG(uint32_t, resp, 1, "Reply size in bytes");
ABSL_FLAG(bool, numa, false, "Whether bind thread to NUMA node");
ABSL_FLAG(bool, profiling, false, "Enable Profiling");
ABSL_FLAG(bool, streaming, false, "Streaming RPCs");
ABSL_FLAG(uint32_t, prepost, 1, "Number of prepost requests");

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using microbenchmark::BenchmarkService;
using microbenchmark::SimpleRequest;
using microbenchmark::SimpleResponse;

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
  void Run() {
    uint16_t port = absl::GetFlag(FLAGS_port);
    uint32_t n_cqs = absl::GetFlag(FLAGS_cqs);
    uint32_t n_threads = absl::GetFlag(FLAGS_threads);
    bool streaming = absl::GetFlag(FLAGS_streaming);
    uint32_t numa = absl::GetFlag(FLAGS_numa);
    bool profiling = absl::GetFlag(FLAGS_profiling);
    uint32_t n_prepost = absl::GetFlag(FLAGS_prepost);

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
              << " Threads: " << n_threads << " NUMA: " << numa
              << " Prepost: " << n_prepost << std::endl;

    running_ = true;
    // Proceed to the server's main loop.
    for (uint32_t tid = 0; tid < n_threads; tid++) {
      ths_.push_back(
          std::thread([this, streaming, tid, numa, profiling, n_prepost]() {
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
            }

            if (streaming) {
              HandleRpcsStreaming(tid, n_prepost);
            } else {
              HandleRpcs(tid, n_prepost);
            }
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
  class CallDataUnary {
   public:
    enum CallStatus { CREATE, PROCESS, FINISH };
    CallDataUnary(BenchmarkService::AsyncService* service,
                  ServerCompletionQueue* cq)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {
      Proceed();
    }

    void Proceed() {
      if (status_ == CREATE) {
        status_ = PROCESS;
        service_->RequestUnaryCall(&ctx_, &request_, &responder_, cq_, cq_,
                                   this);
      } else if (status_ == PROCESS) {
        new CallDataUnary(service_, cq_);
        reply_.mutable_message()->resize(absl::GetFlag(FLAGS_resp));
        status_ = FINISH;
        responder_.Finish(reply_, Status::OK, this);
      } else {
        CHECK(status_ == FINISH);
        delete this;
      }
    }

    CallStatus get_status() const { return status_; }

   private:
    BenchmarkService::AsyncService* service_;
    ServerCompletionQueue* cq_;
    ServerContext ctx_;
    SimpleRequest request_;
    SimpleResponse reply_;
    ServerAsyncResponseWriter<SimpleResponse> responder_;
    CallStatus status_;
  };

  class CallDataStreaming {
   public:
    enum CallStatus { CREATE, REQUEST_DONE, READ_DONE, WRITE_DONE, FINISH };
    CallDataStreaming(BenchmarkService::AsyncService* service,
                      ServerCompletionQueue* cq)
        : service_(service),
          cq_(cq),
          stream_(grpc::ServerAsyncReaderWriter<SimpleResponse, SimpleRequest>(
              &ctx_)),
          status_(CREATE) {
      Proceed(true);
    }

    bool Proceed(bool ok) {
      switch (status_) {
        case CallStatus::CREATE: {
          if (!ok) {
            return false;
          }
          service_->RequestStreamingCall(&ctx_, &stream_, cq_, cq_, this);
          status_ = REQUEST_DONE;
          return true;
        }
        case CallStatus::REQUEST_DONE: {
          new CallDataStreaming(service_, cq_);
          stream_.Read(&request_, this);
          status_ = READ_DONE;
          return true;
        }
        case CallStatus::READ_DONE: {
          reply_.mutable_message()->resize(absl::GetFlag(FLAGS_resp));
          if (ok) {
            stream_.Write(reply_, this);
            status_ = WRITE_DONE;
          } else {  // client has sent writes done
            stream_.Finish(Status::OK, this);
            status_ = FINISH;
          }
          return true;
        }
        case CallStatus::WRITE_DONE: {
          if (ok) {
            stream_.Read(&request_, this);
            status_ = READ_DONE;
          } else {
            stream_.Finish(Status::OK, this);
            status_ = FINISH;
          }
          return true;
        }
        case CallStatus::FINISH: {
          return false;
        }
        default:
          CHECK(false);
      }
    }

    CallStatus get_status() const { return status_; }

   private:
    BenchmarkService::AsyncService* service_;
    ServerCompletionQueue* cq_;
    ServerContext ctx_;
    SimpleRequest request_;
    SimpleResponse reply_;
    grpc::ServerAsyncReaderWriter<SimpleResponse, SimpleRequest> stream_;
    CallStatus status_;
  };

  void HandleRpcs(int tid, uint32_t n_prepost) {
    auto* cq = cqs_[tid % cqs_.size()].get();
    for (int i = 0; i < n_prepost; i++) {
      new CallDataUnary(&service_, cq);
    }

    void* tag;  // uniquely identifies a request.
    bool ok;
    while (running_) {
      CHECK(cq->Next(&tag, &ok));

      if (ok) {
        auto* call = static_cast<CallDataUnary*>(tag);

        call->Proceed();

        if (call->get_status() == CallDataUnary::FINISH) {
          finished_rpcs_[tid]++;
        }
      }
    }
  }

  void HandleRpcsStreaming(int tid, uint32_t n_prepost) {
    auto* cq = cqs_[tid % cqs_.size()].get();
    for (int i = 0; i < n_prepost; i++) {
      new CallDataStreaming(&service_, cq);
    }

    void* tag;  // uniquely identifies a request.
    bool ok;
    while (running_) {
      if (cq->Next(&tag, &ok)) {
        auto* call = static_cast<CallDataStreaming*>(tag);

        if (!call->Proceed(ok)) {
          finished_rpcs_[tid]++;
          new CallDataStreaming(&service_, cq);
        }
      }
    }
  }

  std::vector<std::unique_ptr<ServerCompletionQueue>> cqs_;
  std::vector<uint32_t> finished_rpcs_;
  std::vector<std::thread> ths_;
  BenchmarkService::AsyncService service_;
  std::unique_ptr<Server> server_;
  std::atomic_bool running_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ServerImpl server;

  server.Run();

  return 0;
}
