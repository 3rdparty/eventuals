#include "gtest/gtest.h"

#include "stout/grpc/client.h"
#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

#include "test.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::Notification;

using stout::grpc::Client;
using stout::grpc::ClientCallStatus;
using stout::grpc::ServerBuilder;


TEST_F(StoutGrpcTest, Unimplemented)
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

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials());

  Notification<grpc::Status> finished;

  auto status = client.Call<Greeter, HelloRequest, HelloReply>(
      "SayHello",
      [&](auto&& call, bool ok) {
        EXPECT_TRUE(ok);
        call->OnRead([&](auto* call, auto&& response) {
          EXPECT_TRUE(!response);
          auto status = call->Finish([&](auto*, const grpc::Status& status) {
            finished.Notify(status);
          });
          EXPECT_EQ(ClientCallStatus::Ok, status);
        });
      });

  ASSERT_TRUE(status.ok());

  ASSERT_EQ(grpc::UNIMPLEMENTED, finished.Wait().error_code());
}
