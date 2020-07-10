#include "gtest/gtest.h"

#include "stout/grpc/client.h"
#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

#include "stringify.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::Notification;

using stout::grpc::Client;
using stout::grpc::ClientCallStatus;
using stout::grpc::ServerBuilder;


TEST(GrpcTest, CancelledByClient)
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

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials());

  Notification<grpc::Status> finished;

  auto status = client.Call<Greeter, HelloRequest, HelloReply>(
      "SayHello",
      [&](auto&& call, bool ok) {
        EXPECT_TRUE(ok);
        call->context()->TryCancel();
        auto status = call->Finish([&](auto*, const grpc::Status& status) {
          finished.Notify(status);
        });
        EXPECT_EQ(ClientCallStatus::Ok, status);
      });

  ASSERT_TRUE(status.ok());

  ASSERT_EQ(grpc::CANCELLED, finished.Wait().error_code());
  ASSERT_TRUE(done.Wait());
}
