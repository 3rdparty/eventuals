#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Head;
using eventuals::Let;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, MultipleHosts) {
  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      grpc::InsecureServerCredentials(),
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

  auto [berkeley_cancelled, b] = Terminate(serve("cs.berkeley.edu"));

  b.Start();

  auto [washington_cancelled, w] = Terminate(serve("cs.washington.edu"));

  w.Start();

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      grpc::InsecureChannelCredentials(),
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
