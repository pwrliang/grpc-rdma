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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <numa.h>
#include <numacompat1.h>
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "hdr/hdr_histogram.h"
#include "mpi.h"

#include "micro_benchmark.grpc.pb.h"

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");
ABSL_FLAG(uint32_t, req, 1, "Request size in bytes");
ABSL_FLAG(uint32_t, warmup, 1000, "Number of warmup RPCs");
ABSL_FLAG(uint32_t, rpcs, 1000000, "Number of evaluated RPCs");
ABSL_FLAG(uint32_t, concurrent, 1, "Number of concurrent RPCs");
ABSL_FLAG(uint32_t, duration, 10, "Duration of benchmark in second");
ABSL_FLAG(uint32_t, report_interval, 1, "Report statistics interval in second");
ABSL_FLAG(double, delay, 0, "Sending delay in ms");
ABSL_FLAG(uint32_t, msg_per_stream, 1000,
          "How many RPCs will be sent from a stream");
ABSL_FLAG(bool, coalesce, true, "Enable the coalesce API");
ABSL_FLAG(bool, streaming, false, "Streaming RPCs");
ABSL_FLAG(bool, numa, false, "Enable NUMA");

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::CompletionQueue;
using microbenchmark::BenchmarkService;
using microbenchmark::SimpleRequest;
using microbenchmark::SimpleResponse;

struct Statistics {
  struct hdr_histogram* histogram;
  uint64_t tx_bytes, rx_bytes;
  uint64_t tx_rpcs, rx_rpcs;

  std::chrono::time_point<std::chrono::high_resolution_clock> last_ts;
  uint64_t last_tx_bytes, last_rx_bytes;
  uint64_t last_rx_rpcs;

  Statistics(const Statistics&) = delete;

  Statistics& operator=(const Statistics&) = delete;

  Statistics() {
    hdr_init(1,                                  // Minimum value
             INT64_C(60L * 1000 * 1000 * 1000),  // Maximum value
             3,            // Number of significant figures
             &histogram);  // Pointer to initialise
    Clear();
  }

  ~Statistics() { hdr_close(histogram); }

  void PrintStatistics(int rank, int interval_sec) {
    auto now = std::chrono::high_resolution_clock::now();
    auto past_sec =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_ts)
            .count() /
        1000.0 / 1000.0 / 1000.0;
    if (past_sec >= interval_sec) {
      char host[256];
      gethostname(host, 255);

      auto lat_us_mean = hdr_mean(histogram) / 1000.0;
      auto lat_us_50 = hdr_value_at_percentile(histogram, 0.5) / 1000.0;
      auto lat_us_95 = hdr_value_at_percentile(histogram, 0.95) / 1000.0;
      auto lat_us_99 = hdr_value_at_percentile(histogram, 0.99) / 1000.0;
      auto lat_us_max = hdr_max(histogram) / 1000.0;

      printf(
          "Host %s, Rank %d, Rate %.2f RPCs/s, TX Bandwidth %.2f Mb/s, RX "
          "Bandwidth %.2f Mb/s, RTT (us) mean "
          "%.2f, P50 %.2f, P95 %.2f, P99 %.2f, max %.2f\n",
          host, rank, (rx_rpcs - last_rx_rpcs) / past_sec,
          8e-6 * (tx_bytes - last_tx_bytes) / past_sec,
          8e-6 * (rx_bytes - last_rx_bytes) / past_sec, lat_us_mean, lat_us_50,
          lat_us_95, lat_us_99, lat_us_max);

      last_ts = now;
      last_tx_bytes = tx_bytes;
      last_rx_bytes = rx_bytes;
      last_rx_rpcs = rx_rpcs;
    }
  }

  void Clear() {
    hdr_reset(histogram);
    tx_bytes = 0;
    rx_bytes = 0;
    tx_rpcs = 0;
    rx_rpcs = 0;
    last_ts = std::chrono::high_resolution_clock::now();
    last_tx_bytes = 0;
    last_rx_bytes = 0;
    last_rx_rpcs = 0;
  }
};

struct ClientConfig {
  bool coalesce = true;
  uint32_t messages_per_stream = 1000;
  double delay_ms = 0;
};

class ClientRpcContext {
 public:
  ClientRpcContext() {}
  virtual ~ClientRpcContext() {}
  // next state, return false if done. Collect stats when appropriate
  virtual bool RunNextState(bool ok) = 0;
  virtual void StartNewClone(CompletionQueue* cq) = 0;
  static void* tag(ClientRpcContext* c) { return static_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return static_cast<ClientRpcContext*>(t);
  }

  virtual void Start(CompletionQueue* cq, const ClientConfig& config) = 0;
  virtual void TryCancel() = 0;

  void InsertDelay(double delay_ms) {
    auto begin = absl::Now();
    double past;
    do {
      past = absl::ToDoubleMilliseconds(absl::Now() - begin);
    } while (past < delay_ms);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextUnaryImpl : public ClientRpcContext {
 public:
  ClientRpcContextUnaryImpl(BenchmarkService::Stub* stub,
                            const RequestType& req, Statistics& statistics)
      : stub_(stub),
        req_(req),
        next_state_(State::READY),
        statistics_(statistics) {}

  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    cq_ = cq;
    delay_ms_ = config.delay_ms;
    context_.set_wait_for_ready(true);
    RunNextState(true);
  }

  bool RunNextState(bool ok) override {
    switch (next_state_) {
      case State::READY:
        response_reader_ = stub_->PrepareAsyncUnaryCall(&context_, req_, cq_);

        InsertDelay(delay_ms_);
        issue_ts_ = std::chrono::high_resolution_clock::now();
        statistics_.tx_bytes += req_.ByteSizeLong();
        statistics_.tx_rpcs++;
        response_reader_->StartCall();
        response_reader_->Finish(&reply_, &status_,
                                 ClientRpcContext::tag(this));
        next_state_ = State::RESP_DONE;
        return true;
      case State::RESP_DONE:
        if (status_.ok()) {
          auto now = std::chrono::high_resolution_clock::now();
          auto rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            now - issue_ts_)
                            .count();
          rtt_ns -= delay_ms_ * 1000 * 1000;
          statistics_.rx_bytes += reply_.ByteSizeLong();
          statistics_.rx_rpcs++;
          hdr_record_value(statistics_.histogram, rtt_ns);
        }
        next_state_ = State::INVALID;
        return false;
      default:
        GPR_ASSERT(false);
        return false;
    }
  }

  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextUnaryImpl(stub_, req_, statistics_);
    clone->cq_ = cq;
    clone->delay_ms_ = delay_ms_;
    clone->RunNextState(true);
  }

  void TryCancel() override { context_.TryCancel(); }

 private:
  BenchmarkService::Stub* stub_;
  grpc::CompletionQueue* cq_;
  const RequestType& req_;
  ResponseType reply_;
  grpc::ClientContext context_;
  grpc::Status status_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;
  std::chrono::time_point<std::chrono::high_resolution_clock> issue_ts_;
  Statistics& statistics_;
  double delay_ms_;

  enum State { INVALID, READY, RESP_DONE };
  State next_state_;
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingImpl(BenchmarkService::Stub* stub,
                                const RequestType& req, Statistics& statistics)
      : stub_(stub),
        req_(req),
        next_state_(State::INVALID),
        statistics_(statistics) {}

  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    delay_ms_ = config.delay_ms;
    messages_per_stream_ = config.messages_per_stream;
    coalesce_ = config.coalesce;
    context_.set_wait_for_ready(true);
    StartInternal(cq);
  }

  bool RunNextState(bool ok) override {
    while (true) {
      switch (next_state_) {
        case State::STREAM_IDLE: {
          next_state_ = State::READY_TO_WRITE;
          break;
        }
        case State::READY_TO_WRITE: {
          if (!ok) {
            return false;
          }

          InsertDelay(delay_ms_);
          issue_ts_ = std::chrono::high_resolution_clock::now();
          statistics_.tx_bytes += req_.ByteSizeLong();
          statistics_.tx_rpcs++;
          if (coalesce_ && messages_issued_ == messages_per_stream_ - 1) {
            stream_->WriteLast(req_, grpc::WriteOptions(),
                               ClientRpcContext::tag(this));
          } else {
            stream_->Write(req_, ClientRpcContext::tag(this));
          }
          next_state_ = State::WRITE_DONE;
          return true;
        }
        case State::WRITE_DONE: {
          if (!ok) {
            return false;
          }

          stream_->Read(&reply_, ClientRpcContext::tag(this));
          next_state_ = State::READ_DONE;
          return true;
        }
        case State::READ_DONE: {
          auto now = std::chrono::high_resolution_clock::now();
          auto rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            now - issue_ts_)
                            .count();
          rtt_ns -= delay_ms_ * 1000 * 1000;
          statistics_.rx_bytes += reply_.ByteSizeLong();
          statistics_.rx_rpcs++;
          hdr_record_value(statistics_.histogram, rtt_ns);
          if ((messages_per_stream_ != 0) &&
              (++messages_issued_ >= messages_per_stream_)) {
            next_state_ = State::WRITES_DONE_DONE;
            if (coalesce_) {
              // WritesDone should have been called on the last Write.
              // loop around to call Finish.
              break;
            }
            stream_->WritesDone(ClientRpcContext::tag(this));
            return true;
          }
          next_state_ = State::STREAM_IDLE;
          break;
        }
        case State::WRITES_DONE_DONE: {
          stream_->Finish(&status_, ClientRpcContext::tag(this));
          next_state_ = State::FINISH_DONE;
          return true;
        }
        case State::FINISH_DONE: {
          next_state_ = State::INVALID;
          return false;
        }
        default:
          GPR_ASSERT(false);
      }
    }
  }

  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextStreamingImpl(stub_, req_, statistics_);
    clone->messages_per_stream_ = messages_per_stream_;
    clone->coalesce_ = coalesce_;
    clone->delay_ms_ = delay_ms_;
    clone->StartInternal(cq);
  }

  void TryCancel() override { context_.TryCancel(); }

 private:
  BenchmarkService::Stub* stub_;
  grpc::CompletionQueue* cq_;
  const RequestType& req_;
  ResponseType reply_;
  grpc::ClientContext context_;
  grpc::Status status_;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      stream_;
  // Allow a limit on number of messages in a stream
  int messages_per_stream_;
  int messages_issued_;
  // Whether to use coalescing API.
  bool coalesce_;

  std::chrono::time_point<std::chrono::high_resolution_clock> issue_ts_;
  Statistics& statistics_;
  double delay_ms_;

  enum State {
    INVALID,
    STREAM_IDLE,
    WAIT,
    READY_TO_WRITE,
    WRITE_DONE,
    READ_DONE,
    WRITES_DONE_DONE,
    FINISH_DONE
  };
  State next_state_;

  void StartInternal(CompletionQueue* cq) {
    cq_ = cq;
    next_state_ = State::STREAM_IDLE;
    messages_issued_ = 0;
    if (coalesce_) {
      GPR_ASSERT(messages_per_stream_ != 0);
      context_.set_initial_metadata_corked(true);
    }
    stream_ = stub_->PrepareAsyncStreamingCall(&context_, cq);
    stream_->StartCall(ClientRpcContext::tag(this));
    if (coalesce_) {
      // When the initial metadata is corked, the tag will not come back and we
      // need to manually drive the state machine.
      RunNextState(true);
    }
  }
};

class Client {
 public:
  explicit Client(std::shared_ptr<Channel> channel, const ClientConfig& config)
      : stub_(BenchmarkService::NewStub(channel)), config_(config) {}

  // Assembles the client's payload and sends it to the server.
  void UnaryCall(uint32_t req_size) {
    if (request_.message().size() != req_size) {
      request_.mutable_message()->resize(req_size);
    }
    auto* ctx = new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
        stub_.get(), request_, statistics_);
    ctx->Start(&cq_, config_);
  }

  // Assembles the client's payload and sends it to the server.
  void StreamingCall(uint32_t req_size) {
    if (request_.message().size() != req_size) {
      request_.mutable_message()->resize(req_size);
    }
    auto* ctx =
        new ClientRpcContextStreamingImpl<SimpleRequest, SimpleResponse>(
            stub_.get(), request_, statistics_);
    ctx->Start(&cq_, config_);
  }

  bool AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;

    if (cq_.Next(&got_tag, &ok)) {
      ClientRpcContext* ctx = ClientRpcContext::detag(got_tag);

      if (!ctx->RunNextState(ok)) {
        delete ctx;
        return true;
      }
    }

    return false;
  }

  Statistics& get_statistics() { return statistics_; }

 private:
  std::unique_ptr<BenchmarkService::Stub> stub_;
  uint32_t delay_ms_;
  SimpleRequest request_;
  CompletionQueue cq_;
  Statistics statistics_;
  ClientConfig config_;
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
  bool streaming = absl::GetFlag(FLAGS_streaming);
  bool numa = absl::GetFlag(FLAGS_numa);
  ClientConfig config;

  config.delay_ms = absl::GetFlag(FLAGS_delay);
  config.coalesce = absl::GetFlag(FLAGS_coalesce);
  config.messages_per_stream = absl::GetFlag(FLAGS_msg_per_stream);

  if (rank == 0) {
    printf(
        "Req %u bytes, Warmup %u, RPCs %u, Concurrent %u, Send Delay %.3f ms, "
        "Duration %u secs, Streaming %d, Coalesce %d, msgs/stream %u, "
        "Report Interval %u secs\n",
        req, warmup, rpcs, concurrent, config.delay_ms, duration, streaming,
        config.coalesce, config.messages_per_stream, report_interval);
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

  Client greeter(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()),
      config);
  // Make sure every client runs together
  MPI_Barrier(MPI_COMM_WORLD);
  bool warmup_finish = false;
  auto& statistics = greeter.get_statistics();

run:
  std::chrono::high_resolution_clock::time_point t_begin;
  double past_sec;
  uint32_t issued;

  issued = 0;
  t_begin = std::chrono::high_resolution_clock::now();
  statistics.Clear();

  // Prepost some RPCs
  for (; issued < concurrent; issued++) {
    if (streaming) {
      greeter.StreamingCall(req);
    } else {
      greeter.UnaryCall(req);
    }
  }

  do {
    if (!warmup_finish && statistics.rx_rpcs >= warmup) {
      warmup_finish = true;
      goto run;
    }

    if (issued > 0) {
      if (greeter.AsyncCompleteRpc()) {
        issued--;
      }
    }

    past_sec = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - t_begin)
                   .count() /
               1000.0;
    if (issued < concurrent && statistics.tx_rpcs < rpcs &&
        past_sec < duration) {
      if (streaming) {
        greeter.StreamingCall(req);
      } else {
        greeter.UnaryCall(req);
      }
      issued++;
    }
    statistics.PrintStatistics(rank, report_interval);
  } while (statistics.tx_rpcs < rpcs && past_sec < duration);

  past_sec = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::high_resolution_clock::now() - t_begin)
                 .count() /
             1000.0;

  double rpc_rate = statistics.tx_rpcs / past_sec;
  double tx_bandwidth = statistics.tx_bytes / past_sec * 8e-6;
  double rx_bandwidth = statistics.rx_bytes / past_sec * 8e-6;
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
