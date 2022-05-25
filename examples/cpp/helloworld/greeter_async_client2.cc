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
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include "comm_spec.h"
#include "flags.h"
#include "gflags/gflags.h"
#include "grpcpp/stats_time.h"
#include "stopwatch.h"
#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterClient {
 public:
  explicit GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {
    rest_resp_.store(FLAGS_batch);
  }

  void Warmup(const CommSpec& comm_spec) {
    for (int i = 0; i < FLAGS_warmup; i++) {
      std::string user;
      user.resize(FLAGS_req);
      HelloRequest request;
      request.set_name(user);
      HelloReply reply;
      ClientContext context;
      context.set_wait_for_ready(true);
      Status status = stub_->SayHello(&context, request, &reply);
      GPR_ASSERT(status.ok());
    }
    // Wait for all clients to finish warmup
    MPI_Barrier(comm_spec.comm());
    // Notfiy server to start benchmark
    if (comm_spec.worker_id() == 0) {
      HelloRequest request;
      request.set_start_benchmark(true);
      HelloReply reply;
      ClientContext context;
      Status status = stub_->SayHello(&context, request, &reply);
      GPR_ASSERT(status.ok());
    }
  }

  void SayHello(const std::string& user) {
    GRPCProfiler profiler(GRPC_STATS_TIME_CLIENT_PREPARE);
    HelloRequest request;
    request.set_name(user);

    AsyncClientCall* call = new AsyncClientCall;

    call->response_reader =
        stub_->PrepareAsyncSayHello(&call->context, request, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
  }

  void AsyncCompleteRpc(int batch_size) {
    GRPCProfiler profiler(GRPC_STATS_TIME_CLIENT_CQ_NEXT);
    void* got_tag;
    bool ok = false;

    while (batch_size > 0 && rest_resp_-- > 0) {
      if (cq_.Next(&got_tag, &ok)) {
        AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

        GPR_ASSERT(ok);
        GPR_ASSERT(call->status.ok());
        delete call;
        batch_size--;
      } else {
        rest_resp_++;
      }
    }
  }

  std::atomic_int rest_resp_;

 private:
  struct AsyncClientCall {
    HelloReply reply;
    ClientContext context;
    Status status;

    std::unique_ptr<ClientAsyncResponseReader<HelloReply>> response_reader;
  };

  std::unique_ptr<Greeter::Stub> stub_;
  CompletionQueue cq_;
};

int main(int argc, char** argv) {
  InitMPIComm();

  gflags::SetUsageMessage("Usage: mpiexec [mpi_opts] ./main [main_opts]");
  if (argc == 1) {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "main");
    exit(1);
  }
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (!FLAGS_mode.empty()) {
    setenv("GRPC_PLATFORM_TYPE", FLAGS_mode.c_str(), 1);
  }
  if (FLAGS_sleep > 0) {
    setenv("GRPC_SLEEP", std::to_string(FLAGS_sleep).c_str(), 1);
  }
  if (FLAGS_executor > 0) {
    setenv("GRPC_EXECUTOR", std::to_string(FLAGS_executor).c_str(), 1);
  }
  {
    CommSpec comm_spec;
    comm_spec.Init(MPI_COMM_WORLD);

    GreeterClient greeter(grpc::CreateChannel(
        FLAGS_host + ":50051", grpc::InsecureChannelCredentials()));

    std::vector<std::thread> threads;
    int batch_size = FLAGS_batch;
    Stopwatch sw, sw1;

    greeter.Warmup(comm_spec);

    grpc_stats_time_enable();

    MPI_Barrier(comm_spec.comm());
    sw.start();
    sw1.start();

    for (int i = 0; i < FLAGS_threads; i++) {
      threads.emplace_back([&]() {
        auto chunk_size = (batch_size + FLAGS_threads - 1) / FLAGS_threads;
        std::string user;
        user.resize(FLAGS_req);
        const std::string& s_cli =
            std::to_string(comm_spec.worker_id()) + "_" + std::to_string(i);
        user.copy(const_cast<char*>(s_cli.c_str()), s_cli.length());

        grpc_stats_time_init(0);

        int send_batch_size = FLAGS_poll_num;
        for (int j = 0; j < chunk_size; j++) {
          greeter.SayHello(user);

          if (j % send_batch_size == 0) {
            greeter.AsyncCompleteRpc(send_batch_size);
          }
        }
        greeter.AsyncCompleteRpc(std::numeric_limits<int>::max());
      });
    }

    for (std::thread& th : threads) {
      th.join();
    }
    sw1.stop();

    MPI_Barrier(comm_spec.comm());
    sw.stop();
    double throughput = batch_size / sw.s();
    double total_throughput;
    double time_per_cli = sw1.ms();
    std::vector<double> cli_time_vec(comm_spec.worker_num());

    MPI_Reduce(&throughput, &total_throughput, 1, MPI_DOUBLE, MPI_SUM, 0,
               comm_spec.comm());
    MPI_Gather(&time_per_cli, 1, MPI_DOUBLE, cli_time_vec.data(), 1, MPI_DOUBLE,
               0, comm_spec.comm());

    std::stringstream ss;
    grpc_stats_time_print(ss);

    std::vector<std::string> gathered_stats;
    Gather(comm_spec.comm(), ss.str(), gathered_stats);

    if (comm_spec.worker_id() == 0) {
      for (int i = 0; i < comm_spec.worker_num(); i++) {
        //        printf("Worker: %d time: %lf lat: %lf us\n%s", i, sw1.ms(),
        //               sw1.ms() / batch_size * 1000,
        //               gathered_stats[i].c_str());
        printf("Worker: %d time: %lf lat: %lf us\n%s", i, cli_time_vec[i],
               cli_time_vec[i] / batch_size * 1000, gathered_stats[i].c_str());
      }
      printf("Throughput: %lf req/s\n", total_throughput);
    }
  }
  FinalizeMPIComm();
  gflags::ShutDownCommandLineFlags();
  return 0;
}
