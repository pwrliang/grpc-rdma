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

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "hdr/hdr_histogram.h"
#include "mpi.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <numa.h>
#include <numacompat1.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");
ABSL_FLAG(uint32_t, req, 1, "Request size in bytes");
ABSL_FLAG(uint32_t, warmup, 1000, "Number of warmup RPCs");
ABSL_FLAG(uint32_t, rpcs, 1000000, "Number of evaluated RPCs");
ABSL_FLAG(uint32_t, concurrent, 1, "Number of concurrent RPCs");
ABSL_FLAG(uint32_t, duration, 10, "Duration of benchmark in second");
ABSL_FLAG(uint32_t, report_interval, 1, "Report statistics interval in second");
ABSL_FLAG(bool, numa, false, "Enable NUMA");

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
    // Initialise the histogram
    hdr_init(1,                                  // Minimum value
             INT64_C(60L * 1000 * 1000 * 1000),  // Maximum value
             3,             // Number of significant figures
             &histogram_);  // Pointer to initialise
    tx_bytes_ = 0;
    received_rpcs_ = 0;
    last_ts_ = std::chrono::high_resolution_clock::now();
    last_tx_bytes_ = 0;
    last_received_rpcs_ = 0;
  }

  // Assembles the client's payload and sends it to the server.
  void SayHello(uint32_t req_size, bool wait_conn = false) {
    // Data we are sending to the server.
    HelloRequest request;
    request.mutable_name()->resize(req_size);

    tx_bytes_ += req_size;

    // Call object to store rpc data
    AsyncClientCall* call = new AsyncClientCall;

    call->context.set_wait_for_ready(wait_conn);
    // stub_->PrepareAsyncSayHello() creates an RPC object, returning
    // an instance to store in "call" but does not actually start the RPC
    // Because we are using the asynchronous API, we need to hold on to
    // the "call" instance in order to get updates on the ongoing RPC.
    call->response_reader =
        stub_->PrepareAsyncSayHello(&call->context, request, &cq_);

    call->issue_ts = std::chrono::high_resolution_clock::now();
    // StartCall initiates the RPC call
    call->response_reader->StartCall();

    // Request that, upon completion of the RPC, "reply" be updated with the
    // server's response; "status" with the indication of whether the operation
    // was successful. Tag the request with the memory address of the call
    // object.
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
  }

  // Loop while listening for completed responses.
  // Prints out the response from the server.
  bool AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;

    if (cq_.Next(&got_tag, &ok)) {
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
      GPR_ASSERT(ok);
      GPR_ASSERT(call->status.ok());
      auto now = std::chrono::high_resolution_clock::now();
      auto rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now - call->issue_ts)
                        .count();
      hdr_record_value(histogram_, rtt_ns);
      received_rpcs_++;
      rx_bytes_ += call->reply.ByteSizeLong();
      // Once we're complete, deallocate the call object.
      delete call;
    }

    return ok;
  }

  void PrintStatistics(int rank, int interval_sec) {
    auto now = std::chrono::high_resolution_clock::now();
    auto past_sec =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_ts_)
            .count() /
        1000.0 / 1000.0 / 1000.0;
    if (past_sec >= interval_sec) {
      char host[256];
      gethostname(host, 255);

      auto lat_us_mean = hdr_mean(histogram_) / 1000.0;
      auto lat_us_50 = hdr_value_at_percentile(histogram_, 0.5) / 1000.0;
      auto lat_us_95 = hdr_value_at_percentile(histogram_, 0.95) / 1000.0;
      auto lat_us_99 = hdr_value_at_percentile(histogram_, 0.99) / 1000.0;
      auto lat_us_max = hdr_max(histogram_) / 1000.0;

      printf(
          "Host %s, Rank %d, Rate %.2f RPCs/s, TX Bandwidth %.2f Mb/s, RX "
          "Bandwidth %.2f Mb/s, RTT (us) mean "
          "%.2f, P50 %.2f, P95 %.2f, P99 %.2f, max %.2f\n",
          host, rank, (received_rpcs_ - last_received_rpcs_) / past_sec,
          8e-6 * (tx_bytes_ - last_tx_bytes_) / past_sec,
          8e-6 * (rx_bytes_ - last_rx_bytes_) / past_sec, lat_us_mean,
          lat_us_50, lat_us_95, lat_us_99, lat_us_max);

      last_ts_ = now;
      last_tx_bytes_ = tx_bytes_;
      last_rx_bytes_ = rx_bytes_;
      last_received_rpcs_ = received_rpcs_;
    }
  }

  // struct for keeping state and data information
  struct AsyncClientCall {
    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // Storage for the status of the RPC upon completion.
    Status status;

    std::unique_ptr<ClientAsyncResponseReader<HelloReply>> response_reader;

    std::chrono::time_point<std::chrono::high_resolution_clock> issue_ts;
  };

  void ClearStatistics() {
    hdr_reset(histogram_);
    tx_bytes_ = 0;
    rx_bytes_ = 0;
    received_rpcs_ = 0;
    last_ts_ = std::chrono::high_resolution_clock::now();
    last_tx_bytes_ = 0;
    last_rx_bytes_ = 0;
    last_received_rpcs_ = 0;
  }

  uint64_t get_tx_bytes() const { return tx_bytes_; }

  uint64_t get_rx_bytes() const { return rx_bytes_; }

 private:
  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<Greeter::Stub> stub_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  CompletionQueue cq_;
  struct hdr_histogram* histogram_;
  uint64_t tx_bytes_, rx_bytes_;
  uint64_t received_rpcs_;

  std::chrono::time_point<std::chrono::high_resolution_clock> last_ts_;
  uint64_t last_tx_bytes_, last_rx_bytes_;
  uint64_t last_received_rpcs_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  int rank, n_procs;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &n_procs);

  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  uint32_t req = absl::GetFlag(FLAGS_req);
  uint32_t warmup = absl::GetFlag(FLAGS_warmup);
  uint32_t rpcs = absl::GetFlag(FLAGS_rpcs);
  uint32_t concurrent = absl::GetFlag(FLAGS_concurrent);
  uint32_t duration = absl::GetFlag(FLAGS_duration);
  uint32_t report_interval = absl::GetFlag(FLAGS_report_interval);
  bool numa = absl::GetFlag(FLAGS_numa);

  if (rank == 0) {
    printf(
        "Req %u bytes, Warmup %u, RPCs %u, Concurrent %u, Duration %u secs, "
        "Report Interval %u secs\n",
        req, warmup, rpcs, concurrent, duration, report_interval);
  }

  if (numa) {
    if (numa_available() >= 0) {
      auto nodes = numa_max_node() + 1;
      nodemask_t mask;

      nodemask_zero(&mask);
      nodemask_set(&mask, rank % nodes);
      numa_bind(&mask);
    } else {
      printf("NUMA is not available\n");
      exit(1);
    }
  }
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  GreeterClient greeter(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

  for (int i = 0; i < warmup; i++) {
    greeter.SayHello(req, true);
    greeter.AsyncCompleteRpc();
  }

  // Make sure every client runs together
  MPI_Barrier(MPI_COMM_WORLD);

  greeter.ClearStatistics();

  auto t_begin = std::chrono::high_resolution_clock::now();
  double past_sec;
  uint32_t n_send = 0, n_recv = 0;

  // Prepost some RPCs
  for (; n_send < std::min(rpcs, concurrent); n_send++) {
    greeter.SayHello(req);
  }

  while (n_recv < n_send) {
    if (greeter.AsyncCompleteRpc()) {
      n_recv++;
    }

    past_sec = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - t_begin)
                   .count() /
               1000.0;

    if (n_send < rpcs && past_sec < duration) {
      greeter.SayHello(req);
      n_send++;
    }
    greeter.PrintStatistics(rank, report_interval);
  }

  double rpc_rate = n_recv / past_sec;
  double tx_bandwidth = greeter.get_tx_bytes() / past_sec * 8e-6;
  double rx_bandwidth = greeter.get_rx_bytes() / past_sec * 8e-6;
  double total_rpc_rate, total_tx_bandwidth, total_rx_bandwidth;

  MPI_Reduce(&rpc_rate, &total_rpc_rate, 1, MPI_DOUBLE, MPI_SUM, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&tx_bandwidth, &total_tx_bandwidth, 1, MPI_DOUBLE, MPI_SUM, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&rx_bandwidth, &total_rx_bandwidth, 1, MPI_DOUBLE, MPI_SUM, 0,
             MPI_COMM_WORLD);

  if (rank == 0) {
    printf(
        "Aggregated RPC Rate %.2f RPCs/s, Aggregated TX Bandwidth %.2f Mb/s"
        " RX Bandwidth %.2f Mb/s\n",
        total_rpc_rate, total_tx_bandwidth, total_rx_bandwidth);
  }

  MPI_Finalize();
  return 0;
}
