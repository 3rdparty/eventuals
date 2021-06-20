#include "gtest/gtest.h"

#include "stout/sequence.h"
#include "stout/task.h"

#include "stout/eventuals/grpc/client.h"

#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

#include "test/test.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::borrowable;
using stout::Notification;
using stout::Sequence;

using stout::grpc::ServerBuilder;
using stout::grpc::ServerCallStatus;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Handler;

TEST_F(StoutEventualsGrpcTest, Unary)
{
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

  Notification<bool> done;

  auto serve = server->Serve<Greeter, HelloRequest, HelloReply>(
      "SayHello",
      [](auto* call, auto&& request) {
        EXPECT_TRUE(request);
        HelloReply reply;
        std::string prefix("Hello ");
        reply.set_message(prefix + request->name());
        EXPECT_EQ(
            ServerCallStatus::Ok,
            call->WriteAndFinish(reply, grpc::Status::OK));
      },
      [&](auto*, bool cancelled) {
        done.Notify(cancelled);
      });

  ASSERT_TRUE(serve.ok());

  borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
      | (Handler<grpc::Status>()
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

  ASSERT_TRUE(status.ok());

  ASSERT_FALSE(done.Wait());
}
