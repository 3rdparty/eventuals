#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/grpc/test.h"
#include "test/promisify-for-test.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

TEST(CancelledByClientTest, Cancelled) {
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

  Borrowable<ClientCompletionThreadPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Then(Let([&](ClientCall<HelloRequest, HelloReply>& call) {
             call.context()->TryCancel();
             return call.Finish();
           }));
  };

  auto status = *call();

  EXPECT_EQ(::grpc::CANCELLED, status.error_code());

  EXPECT_TRUE(cancelled.get());
}

} // namespace
} // namespace eventuals::grpc::test
