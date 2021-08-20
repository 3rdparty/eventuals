#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/grpc/client.h"
#include "stout/grpc/server.h"
#include "stout/head.h"
#include "stout/sequence.h"
#include "stout/then.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::borrowable;
using stout::Notification;
using stout::Sequence;

using stout::eventuals::Head;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;

TEST_F(StoutGrpcTest, Unary) {
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
             return Server::Handler(std::move(context))
                 .body([](auto& call, auto&& request) {
                   if (request) {
                     HelloReply reply;
                     std::string prefix("Hello ");
                     reply.set_message(prefix + request->name());
                     call.WriteAndFinish(reply, grpc::Status::OK);
                   }
                 });
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
                 HelloRequest request;
                 request.set_name("emily");
                 call.WriteLast(request);
               })
               .body(Sequence()
                         .Once([](auto& call, auto&& response) {
                           EXPECT_EQ("Hello emily", response->message());
                         })
                         .Once([](auto& call, auto&& response) {
                           EXPECT_FALSE(response);
                         })));
  };

  auto status = *call();

  EXPECT_TRUE(status.ok());

  EXPECT_FALSE(cancelled.get());
}
