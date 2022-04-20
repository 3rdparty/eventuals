#include "eventuals/grpc/client.h"
#include "eventuals/let.h"
#include "eventuals/terminal.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/expect-throw-what.h"
#include "test/grpc/test.h"

namespace eventuals::grpc {
namespace {
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

TEST_F(EventualsGrpcTest, ServerUnavailable) {
  Borrowable<CompletionPool> pool;

  // NOTE: we use 'getpid()' to create a _unique_ UNIX domain socket
  // path that should never have a server listening on for this test.
  Client client(
      "unix:eventuals-grpc-test-server-unavailable-" + std::to_string(getpid()),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then(Let([](auto& call) {
             return call.Finish();
           }));
  };

  EXPECT_THROW_WHAT(*call(), "Failed to start call");
}
} // namespace
} // namespace eventuals::grpc
