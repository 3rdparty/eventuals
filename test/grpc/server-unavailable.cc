#include "eventuals/grpc/client.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/grpc/test.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using testing::StrEq;
using testing::ThrowsMessage;

TEST(ServerUnavailableTest, NonexistantServer) {
  Borrowable<CompletionPool> pool;

  // NOTE: we use 'getpid()' to create a _unique_ UNIX domain socket
  // path that should never have a server listening on for this test.
  Client client(
      "unix:eventuals-grpc-test-server-unavailable-" + std::to_string(getpid()),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Then(Let([](auto& call) {
             return call.Finish();
           }));
  };

  EXPECT_THAT(
      [&]() { *call(); },
      ThrowsMessage<std::runtime_error>(StrEq("Failed to start call")));
}

} // namespace
} // namespace eventuals::grpc::test
