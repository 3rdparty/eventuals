#include "eventuals/grpc/client.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Eventual;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;

// Should only be run from tests!
//
// Expects two arguments.
//
// Expects 'argv[1]' to be a string representing the file descriptor
// that this process has inherited from its parent (the test) that can
// be used to indicate that forking has completed and the test can
// continue.
//
// Expects 'argv[2]' to be a string representing the file descriptor
// that this process has inherited from its parent (the test) that can
// be used to read the bound port of the gRPC server to connect to.
//
// See 'client-death-test.cc' for more details.
int main(int argc, char** argv) {
  // TODO(benh): use stout-flags!
  CHECK_EQ(argc, 3)
      << "expecting 'pipe_fork' and 'pipe_port' to be passed as arguments";

  int pipe_fork = atoi(argv[1]);
  int pipe_port = atoi(argv[2]);

  auto NotifyForked = [&]() {
    int one = 1;
    CHECK_GT(write(pipe_fork, &one, sizeof(int)), 0);
  };

  auto WaitForPort = [&]() {
    int port;
    CHECK_GT(read(pipe_port, &port, sizeof(int)), 0);
    return port;
  };

  NotifyForked();

  int port = WaitForPort();

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then([](auto&& call) {
             // NOTE: to avoid false positives with, for example, one
             // of the 'CHECK()'s failing above, the 'ClientDeathTest'
             // expects the string 'connected' to be written to stderr.
             std::cerr << "connected" << std::endl;
             exit(1);
           });
  };

  *call();

  return 0;
}
