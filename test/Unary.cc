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
using stout::grpc::ServerCallStatus;


TEST(GrpcTest, Unary)
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

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials());

  HelloRequest request;
  request.set_name("emily");

  HelloReply reply;
  Notification<grpc::Status> finished;

  auto status = client.Call<Greeter, HelloRequest, HelloReply>(
      "SayHello",
      &request,
      [&](auto* call, auto&& response) {
        EXPECT_TRUE(response);
        reply.Swap(response.get());
        EXPECT_EQ(ClientCallStatus::Ok, call->Finish());
      },
      [&](auto*, const grpc::Status& status) {
        finished.Notify(status);
      });

  ASSERT_TRUE(status.ok());

  ASSERT_TRUE(finished.Wait().ok());

  ASSERT_EQ("Hello emily", reply.message());

  ASSERT_FALSE(done.Wait());
}
