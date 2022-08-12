#include "eventuals/grpc/client.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "test/grpc/death-constants.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

void RunClient(const int port) {
  Borrowable<ClientCompletionThreadPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Then([](ClientCall<HelloRequest, HelloReply>&& call) {
             // NOTE: need to use 'std::_Exit()' to avoid deadlock
             // waiting for borrowed contexts to get relinquished.
             std::_Exit(kProcessIntentionalExitCode);
           });
  };

  *call();
}

} // namespace
} // namespace eventuals::grpc::test

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

  ::eventuals::grpc::test::RunClient(port);
  return 0;
}
