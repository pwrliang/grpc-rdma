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

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "common.h"
#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

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

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  void SayHello(int idx, const std::string& msg) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(msg);

    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->SayHello(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      GPR_ASSERT(msg == reply.message());
      std::cout << "Greeter " << idx << " received." << std::endl;
    } else {
      std::cout << "RPC failed" << std::endl;
      exit(1);
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

int main(int argc, char** argv) {
  std::string port = "50051";
  if (argc > 1) {
    port = std::string(argv[1]);
  }
  auto server_address = "localhost:" + port;
  printf("Connecting to %s\n", server_address.c_str());

  GreeterClient greeter(
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));
  for (int i = 0; i < TOTAL_N_REQS; i++) {
    auto msg_size = get_msg_size(min_msg_size, max_msg_size);
    auto msg = gen_random_msg(msg_size);

    greeter.SayHello(i, msg);  // The actual RPC call!
  }

  printf("PID: %d, Client Exit\n", getpid());

  return 0;
}
