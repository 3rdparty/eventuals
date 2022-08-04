#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "test/grpc/death-constants.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

void RunServer(const int pipe) {
  auto SendPort = [&](int port) {
    CHECK_GT(write(pipe, &port, sizeof(port)), 0);
  };

  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      ::grpc::InsecureServerCredentials(),
      &port);

  auto build = builder.BuildAndStart();

  CHECK(build.status.ok());

  auto server = std::move(build.server);

  CHECK(server);

  auto serve = [&]() {
    return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Head()
        >> Then([](ServerCall<HelloRequest, HelloReply>&& call) {
             // NOTE: need to use 'std::_Exit()' to avoid deadlock
             // waiting for borrowed contexts to get relinquished.
             std::_Exit(kProcessIntentionalExitCode);
           });
  };

  auto [future, k] = Promisify("death-server", serve());

  k.Start();

  // NOTE: sending this _after_ we start the eventual so that we're
  // ready to accept clients!
  SendPort(port);

  future.get();
}

} // namespace
} // namespace eventuals::grpc::test

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

  ::eventuals::grpc::test::RunServer(pipe);
  return 0;
}
