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

TEST(CancelledByClientTest, CancelledNoFinish) {
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

  auto context = std::make_optional<::grpc::ClientContext>();

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>(
               "SayHello",
               &context.value())
        >> Then(Let([&](ClientCall<HelloRequest, HelloReply>& call) {
             call.context()->TryCancel();
             // NOTE: explicitly not doing a 'Finish()' here to
             // demonstrate that cleaning up the
             // '::grpc::ClientContext' is sufficient.
           }));
  };

  *call();

  CHECK(context.has_value());

  context.reset();

  EXPECT_TRUE(cancelled.get());
}

TEST(CancelledByClientTest, NotCancelledNoFinish) {
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

  auto context = std::make_optional<::grpc::ClientContext>();

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>(
               "SayHello",
               &context.value())
        >> Then(Let([&](ClientCall<HelloRequest, HelloReply>& call) {
             // NOTE: explicitly not doing *either* 'TryCancel()' or
             // 'Finish()' here to demonstrate that cleaning up the
             // '::grpc::ClientContext' is sufficient.
           }));
  };

  *call();

  CHECK(context.has_value());

  context.reset();

  EXPECT_TRUE(cancelled.get());
}

} // namespace
} // namespace eventuals::grpc::test
