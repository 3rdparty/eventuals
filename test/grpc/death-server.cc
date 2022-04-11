// A dummy server used in tests.
// Successfully responds to the 1st RPC then exits with an error on the 2nd.

#include <iostream>

#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using eventuals::Head;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

// Should only be run from tests!
//
// Expects one argument.
//
// Expects as 'argv[1]' a string representing the file descriptor that
// this process has inherited from its parent (the test) that can be
// used to send the bound port of the gRPC server.
//
// See 'server-death-test.cc' for more details.
int main(int argc, char** argv) {
  // TODO(benh): use stout-flags!
  CHECK_EQ(argc, 2) << "expecting 'pipe' to be passed as an argument";

  int pipe = atoi(argv[1]);

  auto SendPort = [&](int port) {
    CHECK_GT(write(pipe, &port, sizeof(port)), 0);
  };

  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      grpc::InsecureServerCredentials(),
      &port);

  auto build = builder.BuildAndStart();

  CHECK(build.status.ok());

  auto server = std::move(build.server);

  CHECK(server);

  auto serve = [&]() {
    return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello")
        | Map(Let([call_count = 0](auto& call) mutable {
             return call.Reader().Read()
                 | Head() // Only get the first element.
                 | Then([&](auto&& request) {
                      ++call_count;
                      std::cout << "Got call " << call_count << std::endl;
                      // Respond to the first call, exit on the second.
                      if (call_count == 1) {
                        std::cout << "Responding to call " << call_count
                                  << std::endl;
                        HelloReply reply;
                        std::string prefix("Hello ");
                        reply.set_message(prefix + request.name());
                        return reply;
                      } else {
                        std::cout << "Server terminating" << std::endl;
                        exit(1);
                      }
                    })
                 | UnaryEpilogue(call);
           }))
        | Loop();
  };

  auto [future, k] = Terminate(serve());

  k.Start();

  // NOTE: sending this _after_ we start the eventual so that we're
  // ready to accept clients!
  std::cout << "Server serving on port " << port << std::endl;
  SendPort(port);

  future.get();
}
