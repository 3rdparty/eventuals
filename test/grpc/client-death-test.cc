#include <sys/wait.h>

#include <cstdlib>

#include "eventuals/grpc/server.hh"
#include "eventuals/head.hh"
#include "eventuals/let.hh"
#include "eventuals/then.hh"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/grpc/death-constants.hh"
#include "test/grpc/test.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Tests that servers correctly handle clients that disconnect before sending a
// request.
TEST(ClientDeathTest, ServerHandlesClientDisconnect) {
  // Start a server that will handle requests.
  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      ::grpc::InsecureServerCredentials(),
      &port);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  auto serve = [&]() {
    return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Head()
        >> Then(Let([](ServerCall<HelloRequest, HelloReply>& call) {
             return call.WaitForDone();
           }));
  };

  auto [cancelled, k] = PromisifyForTest(serve());

  k.Start();

  // Now that the server has started and is ready to accept clients,
  // start the client. It will connect to the server, start a gRPC call, then
  // exit before sending a request.
  const std::string path = GetRunfilePathFor("death-client").string();
  const std::string command = path + " " + std::to_string(port);
  // Block on the client until it returns a known return value.
  const int result = std::system(command.c_str());
  // Issue(#329): WEXITSTATUS is Posix-specific. Figure out the correct way
  // to get the application's return code on windows.
  EXPECT_EQ(kProcessIntentionalExitCode, WEXITSTATUS(result));

  EXPECT_TRUE(cancelled.get());
}

} // namespace
} // namespace eventuals::grpc::test
