#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/eventuals/grpc/client.h"
#include "stout/eventuals/grpc/server.h"
#include "stout/eventuals/head.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::borrowable;

using stout::eventuals::Head;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::grpc::Stream;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;

TEST_F(StoutEventualsGrpcTest, MultipleHosts) {
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
        | Then([](auto&& context) {
             return Server::Handler(std::move(context))
                 .ready([&](auto& call) {
                   call.Finish(grpc::Status::OK);
                 });
           });
  };

  auto [berkeley_cancelled, b] = Terminate(serve("cs.berkeley.edu"));

  b.Start();

  auto [washington_cancelled, w] = Terminate(serve("cs.washington.edu"));

  w.Start();

  borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.borrow());

  auto call = [&](auto&& host) {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello", host)
        | (Client::Handler()
               .ready([](auto& call) {
                 call.WritesDone();
               }));
  };

  auto status = *call("cs.berkeley.edu");

  EXPECT_TRUE(status.ok());

  EXPECT_FALSE(berkeley_cancelled.get());

  status = *call("cs.washington.edu");

  EXPECT_TRUE(status.ok());

  EXPECT_FALSE(washington_cancelled.get());
}
