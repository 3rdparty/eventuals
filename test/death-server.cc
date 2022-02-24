#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using eventuals::Head;
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
        | Head()
        | Then([](auto&& call) {
             // NOTE: to avoid false positives with, for example, one
             // of the 'CHECK()'s failing above, the 'ServerDeathTest'
             // expects the string 'accepted' to be written to stderr.
             std::cerr << "accepted" << std::endl;
             exit(1);
           });
  };

  auto [future, k] = Terminate(serve());

  k.Start();

  // NOTE: sending this _after_ we start the eventual so that we're
  // ready to accept clients!
  SendPort(port);

  future.get();
}
