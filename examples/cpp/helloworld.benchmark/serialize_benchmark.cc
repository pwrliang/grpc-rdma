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
#include <grpcpp/grpcpp.h>
#include <signal.h>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "flags.h"
#include "gflags/gflags.h"
#include "grpcpp/stats_time.h"
#include "proc_parser.h"

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

void grpc_stats_time_print();

void Run(int64_t num_bytes, int64_t max_chunk_bytes = 4ul * 1024 * 1024) {
  void* buf = static_cast<char*>(malloc(num_bytes));
  double serialize_time = 0;
  double pack_time = 0;
  double deserialize_time = 0;
  double unpack_time = 0;
  int n_repeat = 15;

  for (int repeat = 0; repeat < n_repeat; repeat++) {
    std::string str;
    absl::Time t_begin;

    {
      RecvBufRespExtra extra;
      RecvBufRequest request;
      auto rest_num_bytes = num_bytes;
      char* head = static_cast<char*>(buf);

      t_begin = absl::Now();
      while (rest_num_bytes > 0) {
        int64_t bytes = max_chunk_bytes > 0
                            ? std::min(rest_num_bytes, max_chunk_bytes)
                            : rest_num_bytes;
        extra.add_tensor_content(std::string(head, bytes));
        head += bytes;
        rest_num_bytes -= bytes;
      }
      serialize_time += ToDoubleMilliseconds((absl::Now() - t_begin));

      t_begin = absl::Now();

      request.mutable_transport_options()->PackFrom(extra);

      pack_time += ToDoubleMilliseconds((absl::Now() - t_begin));

      str = request.SerializeAsString();
    }

    {
      RecvBufRespExtra extra;
      RecvBufRequest request;
      t_begin = absl::Now();
      request.ParseFromString(str);
      deserialize_time += ToDoubleMilliseconds((absl::Now() - t_begin));

      t_begin = absl::Now();
      request.transport_options().UnpackTo(&extra);
      unpack_time += ToDoubleMilliseconds((absl::Now() - t_begin));
    }
  }

  serialize_time /= n_repeat;
  pack_time /= n_repeat;
  deserialize_time /= n_repeat;
  unpack_time /= n_repeat;
  printf(
      "DataSize: %ld Serialize: %lf ms Pack: %lf ms SER_BW: %lf PACK_BW: "
      "%lf Deserialize: %lf ms Unpack: %lf ms DESER_BW: %lf UNPACK_BW: %lf\n",
      num_bytes, serialize_time, pack_time, num_bytes / (serialize_time * 1000),
      num_bytes / (pack_time * 1000), deserialize_time, unpack_time,
      num_bytes / (deserialize_time * 1000), num_bytes / (unpack_time * 1000));

  free(buf);
}

int main(int argc, char** argv) {
  gflags::SetUsageMessage("Usage: mpiexec [mpi_opts] ./main [main_opts]");
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<int64_t> block_sizes{
      4ul * 1024,         16ul * 1024,
      512ul * 1024,       1ul * 1024 * 1024,
      2ul * 1024 * 1024,  4ul * 1024 * 1024,
      16ul * 1024 * 1024, std::numeric_limits<uint32_t>::max()};

  for (auto block_size : block_sizes) {
    printf("block size: %ld\n", block_size);
    for (int i_pow = 20; i_pow < 29; i_pow++) {
      int64_t data_size = pow(2, i_pow);

      Run(data_size, block_size);
    }
  }
  gflags::ShutDownCommandLineFlags();

  return 0;
}
