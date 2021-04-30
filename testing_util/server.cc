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

#include <grpc++/grpc++.h>

#include "testing_util/helloworld.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using helloworld::HelloReply;
using helloworld::HelloRequest;
using helloworld::HelloWorldService;

// Logic and data behind the server's behavior.
class HelloWorldServiceImpl final : public HelloWorldService::Service {
  Status SayHello(ServerContext *context, const HelloRequest *request,
                  HelloReply *reply) override {
    std::cout << "received request: '" << request->txt() << "'\n";
    std::cout << "context" << context << "\n";
    reply->set_txt("Hello! You said: " + request->txt());
    return Status::OK;
  }

  Status SayHelloStream(ServerContext *context, const HelloRequest *request,
                        ServerWriter<HelloReply> *writer) override {
    std::cout << "received streaming request: '" << request->txt() << "'\n";
    std::cout << "context" << context << "\n";
    for (int i = 0; i < 3; i++) {
      std::cout << "writing streaming reply " << i << std::endl;
      HelloReply reply;
      reply.set_txt("Hello! this is stream response " + std::to_string(i) +
                    " you said: " + request->txt());
      writer->Write(reply);
    }
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  HelloWorldServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char **argv) {
  RunServer();
  return 0;
}
