#include "gtest/gtest.h"

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
using stout::Notification;

using stout::grpc::ServerBuilder;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Handler;

TEST_F(StoutEventualsGrpcTest, CancelledByClient)
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
      [&](auto&& call) {
        // NOTE: not using a 'MockFunction' here because we need to
        // make sure we _wait_ until the callback happens otherwise
        // the test might end and things start to get destructed, like
        // the 'MockFunction' which causes crashes.
        call->OnDone([&](auto*, bool cancelled) {
          done.Notify(cancelled);
        });
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
           call.context().TryCancel();
           call.WritesDone();
         }));
  };

  auto status = *call();

  ASSERT_EQ(grpc::CANCELLED, status.error_code());

  ASSERT_TRUE(done.Wait());
}
