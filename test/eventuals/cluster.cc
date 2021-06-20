#include <atomic>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "stout/context.h"
#include "stout/task.h"

#include "stout/eventuals/grpc/cluster.h"

#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

#include "test/test.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::borrowable;

using stout::grpc::Server;
using stout::grpc::ServerBuilder;
using stout::grpc::ServerCallStatus;

using stout::eventuals::Context;
using stout::eventuals::succeed;

using stout::eventuals::grpc::Cluster;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Handler;

TEST_F(StoutEventualsGrpcTest, Cluster)
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

  borrowable<CompletionPool> pool;

  Cluster cluster(
      { "0.0.0.0:" + stringify(ports[0]),
        "0.0.0.0:" + stringify(ports[1]) },
      grpc::InsecureChannelCredentials(),
      pool);

  auto broadcast = [&]() {
    return cluster.Broadcast<Greeter, HelloRequest, HelloReply>("SayHello")
      | (Handler<size_t>()
         .context(Context<std::atomic<size_t>>(0))
         .ready([](auto&, auto& broadcast, auto& call) {
           HelloRequest request;
           request.set_name("emily");
           call.WriteLast(request);
         })
         .body([](auto&, auto& broadcast, auto& call, auto&& response) {
           if (response) {
             EXPECT_EQ("Hello emily", response->message());
           }
         })
         .finished([](auto& finished, auto& k, auto& broadcast, auto&& status) {
           EXPECT_TRUE(status.ok());
           auto targets = ++*finished;
           if (targets == broadcast.targets()) {
             succeed(k, targets);
           }
         }));
  };

  auto finished = *broadcast();

  ASSERT_EQ(SERVERS, finished);
}
