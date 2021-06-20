#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "stout/task.h"

#include "stout/grpc/server.h"

#include "stout/eventuals/grpc/client.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

#include "test/test.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::borrowable;

using stout::grpc::ServerBuilder;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Handler;

using testing::_;
using testing::MockFunction;

TEST_F(StoutEventualsGrpcTest, CancelledByServer)
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

  MockFunction<void(void*, bool)> mock;

  auto serve = server->Serve<Greeter, HelloRequest, HelloReply>(
      "SayHello",
      [&](auto&& call) {
        call->OnDone(mock.AsStdFunction());

        EXPECT_CALL(mock, Call(_, true))
          .Times(1);

        call->context()->TryCancel();
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
         .body([](auto& call, auto&& response) {
           EXPECT_FALSE(response);
           call.WritesDone();
         }));
  };

  auto status = *call();

  ASSERT_EQ(grpc::CANCELLED, status.error_code());
}
