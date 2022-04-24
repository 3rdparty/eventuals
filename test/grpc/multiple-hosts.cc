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

TEST(MultipleHostsTest, Success) {
  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      ::grpc::InsecureServerCredentials(),
      &port);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok());

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  auto serve = [&](auto&& host) {
    return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello", host)
        | Head()
        | Then(Let([](auto& call) {
             return call.Reader().Read()
                 | Head() // Only get the first element.
                 | Then([](auto&& request) {
                      HelloReply reply;
                      std::string prefix("Hello ");
                      reply.set_message(prefix + request.name());
                      return reply;
                    })
                 | UnaryEpilogue(call);
           }));
  };

  auto [berkeley_cancelled, b] = PromisifyForTest(serve("cs.berkeley.edu"));

  b.Start();

  auto [washington_cancelled, w] = PromisifyForTest(serve("cs.washington.edu"));

  w.Start();

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&](auto&& host) {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello", host)
        | Then(Let([](auto& call) {
             HelloRequest request;
             request.set_name("Emily");
             return call.Writer().WriteLast(request)
                 | call.Reader().Read()
                 | Head() // Expecting but ignoring the response.
                 | call.Finish();
           }));
  };

  auto status = *call("cs.berkeley.edu");

  EXPECT_TRUE(status.ok());

  EXPECT_FALSE(berkeley_cancelled.get());

  status = *call("cs.washington.edu");

  EXPECT_TRUE(status.ok());

  EXPECT_FALSE(washington_cancelled.get());
}

} // namespace
} // namespace eventuals::grpc::test
