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
using helloworld::RecvBufRequest;
using helloworld::RecvBufResponse;
using helloworld::RecvBufRespExtra;

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

  virtual void RecvBuf(size_t size, size_t offset) = 0;

  std::atomic_int rest_resp_;
  std::atomic_size_t total_data_size_;
  size_t this_data_size_ = 0;
  size_t total_rpc_micros_ = 0;
  size_t total_comm_micros_ = 0;
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

  void RecvBuf(size_t size, size_t offset) override {}

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
    req_.mutable_name()->resize(FLAGS_req);
    req_buf_ = SerializeToByteBuffer(&req_);
    recv_buf_ = new uint8_t[1024*1024*256];
  }

  ~GenericClient() {
    delete[] recv_buf_;
  }

  std::unique_ptr<grpc::ByteBuffer> SerializeToByteBuffer(
      grpc::protobuf::Message* message) {
    std::string str;
    message->SerializeToString(&str);

    auto destroy = [](void* buf){
      bool ret = global_sendbuf_free((uint8_t*)buf);
    };

    uint8_t* buf = global_sendbuf_alloc(str.size());
    // uint8_t* buf = nullptr;
    if (buf == nullptr) {
      grpc::Slice slice(str);
      return absl::make_unique<grpc::ByteBuffer>(&slice, 1);
    }
    size_t len = str.length();
    grpc::Slice slice(buf, len, destroy);
    // printf("slice = %p\n", &slice);
    memcpy(buf, str.c_str(), str.size());
    // memset(buf, 7, len);
    return absl::make_unique<grpc::ByteBuffer>(&slice, 1);
  }

  void Warmup(const CommSpec& comm_spec) override {
    for (int i = 0; i < FLAGS_warmup; i++) {
      AsyncClientCall* call = new AsyncClientCall;
      void* got_tag;
      bool ok = false;

      call->context.set_wait_for_ready(true);
      call->response_reader =
          stub_->PrepareUnaryCall(&call->context, methodName, *req_buf_, &cq_);
      call->response_reader->StartCall();
      call->response_reader->Finish(&call->reply, &call->status, (void*)call);
      cq_.Next(&got_tag, &ok);
      GPR_ASSERT(ok);
      AsyncClientCall* got_call = static_cast<AsyncClientCall*>(got_tag);
      checkError(got_call->status);
      delete got_call;
    }
    // Wait for all clients to finish warmup
    MPI_Barrier(comm_spec.comm());
    // Notfiy server to start benchmark
    if (comm_spec.worker_id() == 0) {
      HelloRequest request;
      request.set_start_benchmark(true);
      auto cli_send_buffer = SerializeToByteBuffer(&request);
      AsyncClientCall* call = new AsyncClientCall;
      void* got_tag;
      bool ok = false;

      call->context.set_wait_for_ready(true);
      call->response_reader = stub_->PrepareUnaryCall(
          &call->context, methodName, *cli_send_buffer, &cq_);
      call->response_reader->StartCall();
      call->response_reader->Finish(&call->reply, &call->status, (void*)call);
      cq_.Next(&got_tag, &ok);
      GPR_ASSERT(ok);
      AsyncClientCall* got_call = static_cast<AsyncClientCall*>(got_tag);
      checkError(got_call->status);
      delete got_call;
    }
  }

  void SayHello(const std::string& user) override {
    GRPCProfiler profiler(GRPC_STATS_TIME_CLIENT_PREPARE);
    total_data_size_ += req_buf_->Length();
    AsyncClientCall* call = new AsyncClientCall;

    call->response_reader =
        stub_->PrepareUnaryCall(&call->context, methodName, *req_buf_, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
  }

  void RecvBuf(size_t size, size_t offset) override {
    recv_buf_req_.set_num_bytes(size);
    recv_buf_req_.set_offset(offset);
    bool own_buffer;
    grpc::GenericSerialize<grpc::ProtoBufferWriter, grpc::protobuf::Message>(recv_buf_req_, &recv_buf_req_buf_, &own_buffer);
    // printf("req buf = %lld\n", recv_buf_req_buf_.Length());
    AsyncClientCall* call = new AsyncClientCall;
    call->context.set_wait_for_ready(true);
    call->recv_buf_enable = true;
    call->response_reader = stub_->PrepareUnaryCall(&call->context, recv_buf_method_name_, recv_buf_req_buf_, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
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
        this_data_size_ = call->reply.Length();
        // printf("reply length = %lld\n", call->reply.Length());
        if (call->recv_buf_enable) {
          Stopwatch sw;
          sw.start();
          grpc::ProtoBufferReader reader(&call->reply);
          RecvBufResponse resp;
          resp.ParseFromZeroCopyStream(&reader);
          RecvBufRespExtra extra;
          resp.transport_options().UnpackTo(&extra);
          uint8_t* head = recv_buf_;
          for (const auto& tensor_content_chunk : extra.tensor_content()){
            memcpy(head, std::string(tensor_content_chunk).data(), tensor_content_chunk.size());
            head += tensor_content_chunk.size();
          }
          sw.stop();
          ret = resp.micros() + sw.us();
        }
        
        delete call;
      } else {
        rest_resp_++;
        gpr_log(GPR_ERROR, "RPC Failed");
      }
    }
    return ret;
  }

  void NotifyFinish() override {
    std::string user = "fin";
    HelloRequest request;
    request.set_name(user);
    auto cli_send_buffer = SerializeToByteBuffer(&request);
    AsyncClientCall* call = new AsyncClientCall;
    void* got_tag;
    bool ok = false;

    call->context.set_wait_for_ready(true);
    call->response_reader = stub_->PrepareUnaryCall(&call->context, methodName,
                                                    *cli_send_buffer, &cq_);
    call->response_reader->StartCall();
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
    cq_.Next(&got_tag, &ok);
    GPR_ASSERT(ok);
    AsyncClientCall* got_call = static_cast<AsyncClientCall*>(got_tag);
    checkError(got_call->status);
    delete got_call;
  }

 private:
  struct AsyncClientCall {
    grpc::ByteBuffer reply;
    ClientContext context;
    Status status;
    bool recv_buf_enable = false;

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
  std::string methodName = "/helloworld.Greeter/SayHello";
  HelloRequest req_;
  std::unique_ptr<grpc::ByteBuffer> req_buf_;

  RecvBufRequest recv_buf_req_;
  grpc::ByteBuffer recv_buf_req_buf_;
  std::string recv_buf_method_name_ = "/helloworld.Greeter/RecvBuf";
  uint8_t* recv_buf_;
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
    Stopwatch sw, sw1;

    // cli->Warmup(comm_spec);

    grpc_stats_time_enable();

    for (size_t i = 0; i < FLAGS_warmup; i++) {
      cli->RecvBuf(FLAGS_req, 0);
      cli->AsyncCompleteRpc();
    }

    cli->total_data_size_ = 0;
    cli->total_comm_micros_ = 0;
    cli->total_rpc_micros_ = 0;
    cli->rest_resp_ = batch_size;

    MPI_Barrier(comm_spec.comm());
    sw.start();
    sw1.start();

    for (int i = 0; i < FLAGS_threads; i++) {
      threads.emplace_back([&]() {
        auto chunk_size = (batch_size + FLAGS_threads - 1) / FLAGS_threads;
        // std::string user;
        // user.resize(FLAGS_req);
        // const std::string& s_cli =
        //     std::to_string(comm_spec.worker_id()) + "_" + std::to_string(i);
        // user.copy(const_cast<char*>(s_cli.c_str()), s_cli.length());

        grpc_stats_time_init(0);

        size_t offset = 0;

        for (int j = 0; j < chunk_size; j++) {
          // cli->SayHello(user);
          // printf("batch %lld RecvBuf\n", j);
          Stopwatch sw2;
          sw2.start();
          cli->RecvBuf(FLAGS_req, offset);
          // printf("batch %lld AsyncCompleteRpc\n", j);
          int64_t exec_micros = cli->AsyncCompleteRpc();
          sw2.stop();
          int64_t rpc_micros = sw2.us();
          size_t data_size = cli->this_data_size_;
          double comm_band = double(data_size) / (rpc_micros - exec_micros);
          double band = double(data_size) / rpc_micros;
          cli->total_comm_micros_ += rpc_micros - exec_micros;
          cli->total_rpc_micros_ += rpc_micros;
          printf("%lld: data size = %lld bytes, rpc micros = %lld, exec micros = %lld, comm micros = (%lld, %.4lf), bandwidth = (%.4lf, %.4lf) MB/s\n", 
            j, data_size, rpc_micros, exec_micros, rpc_micros - exec_micros, double(rpc_micros - exec_micros) / rpc_micros, comm_band, band);

          // printf("batch %lld done\n", j);

          offset = (offset + FLAGS_req) % (128*1024*1024);

          auto send_interval_us = FLAGS_send_interval;
          absl::Time begin_poll = absl::Now();
          while ((absl::Now() - begin_poll) <
                 absl::Microseconds(send_interval_us)) {
          }
        }

        size_t total_data_size = cli->total_data_size_.load();
        size_t total_rpc_micros = cli->total_rpc_micros_;
        size_t total_comm_micros = cli->total_comm_micros_;
        size_t total_exec_micros = total_rpc_micros - total_comm_micros;
        printf("total data size = %lld bytes, total time = %lld us, total exec time = %lld us, total comm time = (%lld us, %.4lf), bandwidth = (%.4lf, %.4lf) MB/s\n",
          total_data_size, total_rpc_micros, total_exec_micros, total_comm_micros, double(total_comm_micros) / total_rpc_micros,
          double(total_data_size) / total_comm_micros, double(total_data_size) / total_rpc_micros);

      });
    }

    for (std::thread& th : threads) {
      th.join();
    }
    sw1.stop();

    MPI_Barrier(comm_spec.comm());
    sw.stop();
    double throughput = batch_size / sw.s();
    double bandwidth = (cli->total_data_size_ / 1024.0 / 1024) / sw.s();
    double total_throughput;
    double time_per_cli = sw1.ms();
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
      // cli->NotifyFinish();
    }

    delete cli;

  }

  FinalizeMPIComm();
  gflags::ShutDownCommandLineFlags();
  return 0;
}
