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

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <thread>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "common.h"

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
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload and sends it to the server.
  void SayHello(int idx, const std::string& msg) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(msg);
    GPR_ASSERT(request.ByteSizeLong() <= 4194304);

    // Call object to store rpc data
    AsyncClientCall* call = new AsyncClientCall;

    call->idx = idx;
    call->msg = msg;
    call->context.set_wait_for_ready(true);
    // stub_->PrepareAsyncSayHello() creates an RPC object, returning
    // an instance to store in "call" but does not actually start the RPC
    // Because we are using the asynchronous API, we need to hold on to
    // the "call" instance in order to get updates on the ongoing RPC.
    call->response_reader =
        stub_->PrepareAsyncSayHello(&call->context, request, &cq_);

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
  void AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    while (total_req > 0 && cq_.Next(&got_tag, &ok)) {
      // The tag in this example is the memory location of the call object
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

      // Verify that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by Finish().
      GPR_ASSERT(ok);

      if (call->status.ok()) {
        GPR_ASSERT(call->msg == call->reply.message());
        std::cout << "Greeter " << call->idx << " received." << std::endl;
        total_req--;
      } else {
        std::cout << "RPC failed, error code " << call->status.error_code()
                  << ", details " << call->status.error_details() << " msg, "
                  << call->status.error_message() << std::endl;
        exit(1);
      }

      // Once we're complete, deallocate the call object.
      delete call;
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
    int idx;
    std::string msg;
  };

  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<Greeter::Stub> stub_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  CompletionQueue cq_;
  int total_req;
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).
  std::string port = "50051";
  if (argc > 1) {
    port = std::string(argv[1]);
  }
  auto server_address = "localhost:" + port;
  printf("Connecting to %s\n", server_address.c_str());

  GreeterClient greeter(
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

  int total_req = TOTAL_N_REQS;

  greeter.total_req = total_req;

  // Spawn reader thread that loops indefinitely
  std::thread thread_ = std::thread(&GreeterClient::AsyncCompleteRpc, &greeter);

  for (int i = 0; i < total_req; i++) {
    auto msg_size = get_msg_size(min_msg_size, max_msg_size);
    auto msg = gen_random_msg(msg_size);

    greeter.SayHello(i, msg);  // The actual RPC call!
  }

  thread_.join();

  printf("PID: %d, Client Exit\n", getpid());

  return 0;
}
