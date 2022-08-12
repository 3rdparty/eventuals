#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
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

void TestUnaryWithClient(
    const std::function<Client(
        stout::borrowed_ref<CompletionThreadPool>&&,
        int)>& client_factory) {
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
        >> Then(Let([](auto& call) {
             return call.Reader().Read()
                 >> Head() // Only get the first element.
                 >> Then([](HelloRequest&& request) {
                      HelloReply reply;
                      std::string prefix("Hello ");
                      reply.set_message(prefix + request.name());
                      return reply;
                    })
                 >> UnaryEpilogue(call);
           }));
  };

  auto [cancelled, k] = PromisifyForTest(serve());

  k.Start();

  Borrowable<CompletionThreadPool> pool;

  Client client = client_factory(pool.Borrow(), port);

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Then(Let([](auto& call) {
             HelloRequest request;
             request.set_name("emily");
             return call.Writer().WriteLast(request)
                 >> call.Reader().Read()
                 >> Map([](HelloReply&& response) {
                      EXPECT_EQ("Hello emily", response.message());
                    })
                 >> Loop()
                 >> call.Finish();
           }));
  };

  auto status = *call();

  EXPECT_TRUE(status.ok()) << status.error_code()
                           << ": " << status.error_message();

  EXPECT_FALSE(cancelled.get());

  // NOTE: explicitly calling 'Shutdown()' and 'Wait()' to test that
  // they can be called safely since the destructor for a server
  // _also_ trys to call them .
  server->Shutdown();
  server->Wait();
}

TEST(UnaryTest, SuccessWithDefaultChannel) {
  TestUnaryWithClient(
      [](stout::borrowed_ref<CompletionThreadPool>&& pool, const int port) {
        // Have the client construct its own channel.
        return Client(
            "0.0.0.0:" + std::to_string(port),
            ::grpc::InsecureChannelCredentials(),
            std::move(pool));
      });
}

TEST(UnaryTest, SuccessWithCustomChannel) {
  TestUnaryWithClient(
      [](stout::borrowed_ref<CompletionThreadPool>&& pool, const int port) {
        // Have the client use a channel that we've constructed ourselves.
        std::shared_ptr<::grpc::Channel> channel =
            ::grpc::CreateChannel(
                "0.0.0.0:" + std::to_string(port),
                ::grpc::InsecureChannelCredentials());
        return Client(channel, std::move(pool));
      });
}

} // namespace
} // namespace eventuals::grpc::test
