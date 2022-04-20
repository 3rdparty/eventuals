#include "eventuals/grpc/client.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "test/grpc/death-constants.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;

void RunClient(const int port) {
  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then([](auto&& call) {
             exit(eventuals::grpc::kProcessIntentionalExitCode);
           });
  };

  *call();
}

// Should only be run from tests!
//
// Expects one argument.
//
// Expects 'argv[1]' to be an integer representing the port number that should
// be used to connect to a gRPC server.
// See 'client-death-test.cc' for more details.
int main(int argc, char** argv) {
  // TODO(benh): use stout-flags!
  CHECK_EQ(argc, 2) << "expecting 'port' to be passed as an argument";

  int port = atoi(argv[1]);

  RunClient(port);
  return 0;
}
