#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/eventuals/grpc/client.h"
#include "stout/eventuals/grpc/server.h"
#include "stout/eventuals/head.h"
#include "stout/then.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::borrowable;

using stout::eventuals::Head;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;

TEST_F(StoutGrpcTest, CancelledByClient) {
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

  auto serve = [&]() {
    return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello")
        | Head()
        | Then([](auto&& context) {
             return Server::Handler(std::move(context));
           });
  };

  auto [cancelled, k] = Terminate(serve());

  k.Start();

  borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | (Client::Handler()
               .ready([](auto& call) {
                 call.context().TryCancel();
                 call.WritesDone();
               }));
  };

  auto status = *call();

  EXPECT_EQ(grpc::CANCELLED, status.error_code());

  EXPECT_TRUE(cancelled.get());
}
