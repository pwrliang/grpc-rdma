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

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "comm_spec.h"
#include "flags.h"
#include "gflags/gflags.h"
#include "stopwatch.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}
  void Warmup() {
    std::string user;
    user.resize(FLAGS_req);

    for (int i = 0; i < FLAGS_warmup; i++) {
      SayHello(user);
    }
  }

  std::string SayHello(const std::string& user) {
    HelloRequest request;
    request.set_name(user);
    HelloReply reply;
    ClientContext context;
    Status status = stub_->SayHello(&context, request, &reply);
    GPR_ASSERT(status.ok());

    return reply.message();
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

int main(int argc, char** argv) {
  InitMPIComm();

  gflags::SetUsageMessage("Usage: mpiexec [mpi_opts] ./main [main_opts]");
  if (argc == 1) {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "main");
    exit(1);
  }
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::ShutDownCommandLineFlags();
  {
    CommSpec comm_spec;
    comm_spec.Init(MPI_COMM_WORLD);

    GreeterClient greeter(grpc::CreateChannel(
        FLAGS_host + ":50051", grpc::InsecureChannelCredentials()));

    int batch_size = FLAGS_batch;
    std::vector<std::thread> threads;
    Stopwatch sw;
    greeter.Warmup();

    sw.start();
    MPI_Barrier(comm_spec.client_comm());
    for (int i = 0; i < FLAGS_threads; i++) {
      threads.emplace_back([&]() {
        auto chunk_size = (batch_size + FLAGS_threads - 1) / FLAGS_threads;
        std::string user;
        user.resize(FLAGS_req);

        for (int j = 0; j < chunk_size; j++) {
          auto resp = greeter.SayHello(user);
        }
      });
    }

    for (std::thread& th : threads) {
      th.join();
    }
    MPI_Barrier(comm_spec.client_comm());
    sw.stop();
    double throughput = batch_size / sw.s();
    double total_throughput;
    MPI_Reduce(&throughput, &total_throughput, 1, MPI_DOUBLE, MPI_SUM, 0,
               comm_spec.comm());
    if (comm_spec.worker_id() == 0) {
      std::cout << "Throughput: " << total_throughput << " req/s" << std::endl;
    }
  }
  FinalizeMPIComm();
  return 0;
}
