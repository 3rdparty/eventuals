#include <atomic>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "stout/grpc/cluster.h"
#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

#include "test.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::Notification;

using stout::grpc::Cluster;
using stout::grpc::ClientCallStatus;
using stout::grpc::Server;
using stout::grpc::ServerBuilder;
using stout::grpc::ServerCallStatus;


TEST_F(StoutGrpcTest, Cluster)
{
  const size_t SERVERS = 2;

  std::vector<std::unique_ptr<Server>> servers;
  std::vector<int> ports;

  for (size_t i = 0; i < SERVERS; i++) {
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
        [](auto*, bool cancelled) {
          EXPECT_FALSE(cancelled);
        });

    ASSERT_TRUE(serve.ok());

    servers.push_back(std::move(server));
    ports.push_back(port);
  }

  ASSERT_EQ(SERVERS, ports.size());

  Cluster cluster(
      { "0.0.0.0:" + stringify(ports[0]),
        "0.0.0.0:" + stringify(ports[1]) },
      grpc::InsecureChannelCredentials());

  HelloRequest request;
  request.set_name("emily");

  std::atomic<size_t> responses(0);
  Notification<bool> finished;

  auto status = cluster.Broadcast<Greeter, HelloRequest, HelloReply>(
      "SayHello",
      &request,
      [&](auto* call, auto&& response) {
        EXPECT_TRUE(response);
        EXPECT_EQ("Hello emily", response->message());
        EXPECT_EQ(ClientCallStatus::Ok, call->Finish());
        EXPECT_LT(responses.load(), SERVERS);
        responses++;
      },
      [&](auto* call, const grpc::Status& status) {
        EXPECT_TRUE(status.ok());
        if (responses.load() == SERVERS) {
          finished.Notify(true);
        }
      });

  ASSERT_TRUE(status.ok());

  ASSERT_TRUE(finished.Wait());
}
