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
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/codegen/proto_utils.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include "absl/memory/memory.h"
#include "bytebuffer_util.h"
#include "comm_spec.h"
#include "flags.h"
#include "gflags/gflags.h"
#include "grpcpp/stats_time.h"
#include "object_pool.h"
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
using helloworld::RecvBufRequest;
using helloworld::RecvBufRespExtra;
using helloworld::RecvBufResponse;

class Client {
 public:
  Client() {
    rest_resp_ = FLAGS_batch;
    total_data_size_ = 0;
  }

  virtual ~Client() {}

  virtual void Warmup(const CommSpec& comm_spec) = 0;

  virtual void SayHello(const std::string& user) = 0;

  virtual size_t AsyncCompleteRpc() = 0;

  virtual void NotifyFinish() = 0;

  std::atomic_int rest_resp_;
  std::atomic_size_t total_data_size_;
};

class GreeterClient : public Client {
 public:
  explicit GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  void Warmup(const CommSpec& comm_spec) override {
    for (int i = 0; i < FLAGS_warmup; i++) {
      std::string user;
      user.resize(FLAGS_req);
      HelloRequest request;
      request.set_name(user);
      HelloReply reply;
      ClientContext context;
      context.set_wait_for_ready(true);
      Status status = stub_->SayHello(&context, request, &reply);
      checkError(status);
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
      checkError(status);
    }
  }

  void SayHello(const std::string& user) override {
    GRPCProfiler profiler(GRPC_STATS_TIME_CLIENT_PREPARE);
    HelloRequest request;
    request.set_name(user);
    total_data_size_ += request.ByteSizeLong();
    AsyncClientCall* call = new AsyncClientCall;

    call->response_reader =
        stub_->PrepareAsyncSayHello(&call->context, request, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
  }

  size_t AsyncCompleteRpc() override {
    GRPCProfiler profiler(GRPC_STATS_TIME_CLIENT_CQ_NEXT);
    void* got_tag;
    bool ok = false;

    if (rest_resp_-- > 0) {
      if (cq_.Next(&got_tag, &ok)) {
        AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

        GPR_ASSERT(ok);
        GPR_ASSERT(call->status.ok());
        total_data_size_ += call->reply.ByteSizeLong();
        delete call;
      } else {
        rest_resp_++;
        gpr_log(GPR_ERROR, "RPC Failed");
      }
    }
    return 0;
  }

  void NotifyFinish() override {
    std::string user = "fin";
    HelloRequest request;
    request.set_name(user);
    HelloReply reply;
    ClientContext context;
    context.set_wait_for_ready(true);
    Status status = stub_->SayHello(&context, request, &reply);
    checkError(status);
  }

 private:
  struct AsyncClientCall {
    HelloReply reply;
    ClientContext context;
    Status status;

    std::unique_ptr<ClientAsyncResponseReader<HelloReply>> response_reader;
  };

  void checkError(grpc::Status& s) {
    if (!s.ok()) {
      printf("%s\n", s.error_message().c_str());
    }
    GPR_ASSERT(s.ok());
  }

  std::unique_ptr<Greeter::Stub> stub_;
  CompletionQueue cq_;
};

class GenericClient : public Client {
 public:
  explicit GenericClient(std::shared_ptr<Channel> channel)
      : stub_(new grpc::GenericStub(channel)) {
  }


  void Warmup(const CommSpec& comm_spec) override {
    auto rpc = [this](const grpc::ByteBuffer& byte_buffer) {
      void* got_tag;
      bool ok = false;
      AsyncClientCall call;

      call.context.set_wait_for_ready(true);
      call.response_reader = stub_->PrepareUnaryCall(&call.context, methodName_,
                                                     byte_buffer, &cq_);
      call.response_reader->StartCall();
      call.response_reader->Finish(&call.reply, &call.status, &call);
      checkError(call.status);

      GPR_ASSERT(cq_.Next(&got_tag, &ok));
      GPR_ASSERT(ok);
    };

    std::string s_req;

    s_req.resize(FLAGS_req);
    grpc::ByteBuffer req_byte_buffer = StringToByteBuffer(s_req);

    for (int i = 0; i < FLAGS_warmup; i++) {
      rpc(req_byte_buffer);
    }
    // Wait for all clients to finish warmup
    MPI_Barrier(comm_spec.comm());
    // Notfiy server to start benchmark
    if (comm_spec.worker_id() == 0) {
      s_req[0] = 0xab;
      req_byte_buffer = StringToByteBuffer(s_req);
      rpc(req_byte_buffer);
    }
  }

  void SayHello(const std::string& user) override {
    AsyncClientCall* call = new AsyncClientCall;

    req_buf_ = StringToByteBuffer(user);
    call->context.set_wait_for_ready(true);
    call->response_reader =
        stub_->PrepareUnaryCall(&call->context, methodName_, req_buf_, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, call);
    total_data_size_ += req_buf_.Length();
  }

  size_t AsyncCompleteRpc() override {
    GRPCProfiler profiler(GRPC_STATS_TIME_CLIENT_CQ_NEXT);
    void* got_tag;
    bool ok = false;
    int64_t ret = 0;

    if (rest_resp_-- > 0) {
      if (cq_.Next(&got_tag, &ok)) {
        AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

        GPR_ASSERT(ok);
        checkError(call->status);
        total_data_size_ += call->reply.Length();
        gpr_log(GPR_INFO, "Got len: %zu\n", call->reply.Length());
        delete call;
      } else {
        rest_resp_++;
        gpr_log(GPR_ERROR, "RPC Failed");
      }
    }
    return ret;
  }

  void NotifyFinish() override {}

 private:
  struct AsyncClientCall {
    grpc::ByteBuffer reply;
    ClientContext context;
    Status status;

    std::unique_ptr<ClientAsyncResponseReader<grpc::ByteBuffer>>
        response_reader;
  };

  void checkError(grpc::Status& s) {
    if (!s.ok()) {
      printf("%s\n", s.error_message().c_str());
    }
    GPR_ASSERT(s.ok());
  }

  std::unique_ptr<grpc::GenericStub> stub_;
  CompletionQueue cq_;
  std::string methodName_ = "This can be any name";
  grpc::ByteBuffer req_buf_;
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
  if (FLAGS_executor > 0) {
    setenv("GRPC_EXECUTOR", std::to_string(FLAGS_executor).c_str(), 1);
  }
  {
    CommSpec comm_spec;
    comm_spec.Init(MPI_COMM_WORLD);

    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(-1);
    auto channel = grpc::CreateCustomChannel(
        FLAGS_host + ":50051", grpc::InsecureChannelCredentials(), args);

    Client* cli;
    if (FLAGS_generic) {
      cli = new GenericClient(channel);
    } else {
      cli = new GreeterClient(channel);
    }

    std::vector<std::thread> threads;
    int batch_size = FLAGS_batch;
    Stopwatch global_sw, local_sw;

    cli->Warmup(comm_spec);

    MPI_Barrier(comm_spec.comm());
    global_sw.start();
    local_sw.start();

    grpc_stats_time_enable();

    std::string user;
    user.resize(FLAGS_req);

    for (int i = 0; i < FLAGS_threads; i++) {
      threads.emplace_back([&]() {
        auto chunk_size = (batch_size + FLAGS_threads - 1) / FLAGS_threads;

        grpc_stats_time_init(i);

        for (int j = 0; j < chunk_size; j++) {
          cli->SayHello(user);
          cli->AsyncCompleteRpc();

          auto send_interval_us = FLAGS_send_interval;
          absl::Time begin_poll = absl::Now();
          while ((absl::Now() - begin_poll) <
                 absl::Microseconds(send_interval_us)) {
          }
        }
      });
    }

    for (std::thread& th : threads) {
      th.join();
    }
    local_sw.stop();

    MPI_Barrier(comm_spec.comm());
    global_sw.stop();
    double throughput = batch_size / global_sw.s();
    double bandwidth = (cli->total_data_size_ / 1024.0 / 1024) / global_sw.s();
    double total_throughput;
    double time_per_cli = local_sw.ms();
    std::vector<double> cli_time_vec(comm_spec.worker_num());
    std::vector<double> cli_bandwidth_vec(comm_spec.worker_num());

    MPI_Reduce(&throughput, &total_throughput, 1, MPI_DOUBLE, MPI_SUM, 0,
               comm_spec.comm());
    MPI_Gather(&time_per_cli, 1, MPI_DOUBLE, cli_time_vec.data(), 1, MPI_DOUBLE,
               0, comm_spec.comm());
    MPI_Gather(&bandwidth, 1, MPI_DOUBLE, cli_bandwidth_vec.data(), 1,
               MPI_DOUBLE, 0, comm_spec.comm());

    std::stringstream ss;
    grpc_stats_time_print(ss);

    std::vector<std::string> gathered_stats;
    Gather(comm_spec.comm(), ss.str(), gathered_stats);

    if (comm_spec.worker_id() == 0) {
      double avg_lat_ms = 0, min_lat_ms = std::numeric_limits<double>::max(),
             max_lat_ms = 0;
      for (int i = 0; i < comm_spec.worker_num(); i++) {
        auto lat_per_cli = cli_time_vec[i] / batch_size;
        avg_lat_ms += lat_per_cli;
        min_lat_ms = std::min(min_lat_ms, lat_per_cli);
        max_lat_ms = std::max(max_lat_ms, lat_per_cli);
        printf("Worker: %d time: %lf lat: %lf us Bandwidth: %lf MB/s\n%s", i,
               cli_time_vec[i], lat_per_cli * 1000, cli_bandwidth_vec[i],
               gathered_stats[i].c_str());
      }
      avg_lat_ms /= comm_spec.worker_num();
      printf("Throughput: %lf req/s, avg latency: %f us, [%f, %f]\n",
             total_throughput, avg_lat_ms * 1000, min_lat_ms * 1000,
             max_lat_ms * 1000);
    }

    delete cli;
  }

  FinalizeMPIComm();
  gflags::ShutDownCommandLineFlags();

  return 0;
}
