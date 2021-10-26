#include <atomic>
#include <string>
#include <vector>

#include "eventuals/context.h"
#include "eventuals/grpc/cluster.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Context;
using eventuals::Head;
using eventuals::Let;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::Cluster;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, BroadcastCancel) {
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

    servers.push_back(std::move(server));
    ports.push_back(port);
  }

  ASSERT_EQ(SERVERS, ports.size());

  auto serve = [](std::unique_ptr<Server>& server) {
    return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello")
        | Head()
        | Then(Let([](auto& call) {
             return call.WaitForDone();
           }));
  };

  using K = std::decay_t<
      decltype(std::get<1>(Terminate(serve(servers.front()))))>;

  std::deque<K> ks;

  for (auto& server : servers) {
    auto [_, k] = Terminate(serve(server));
    ks.emplace_back(std::move(k)).Start();
  }

  Borrowable<CompletionPool> pool;

  Cluster cluster(
      {"0.0.0.0:" + stringify(ports[0]),
       "0.0.0.0:" + stringify(ports[1])},
      grpc::InsecureChannelCredentials(),
      pool);

  struct Atomics {
    std::atomic<size_t> ready = 0;
    std::atomic<size_t> finished = 0;
  };

  auto broadcast = [&]() {
    return cluster.Broadcast<Greeter, HelloRequest, HelloReply>("SayHello")
        | (Client::Handler<size_t>()
               .context(Context<Atomics>())
               .ready([](auto& atomics, auto& broadcast, auto& call) {
                 call.WritesDone();
                 if (++atomics->ready == broadcast.targets()) {
                   broadcast.TryCancel();
                 }
               })
               .finished(
                   [=](auto& atomics, auto& k, auto& broadcast, auto&& status) {
                     EXPECT_EQ(grpc::CANCELLED, status.error_code());
                     auto targets = ++atomics->finished;
                     if (targets == broadcast.targets()) {
                       k.Start(targets);
                     }
                   }));
  };

  auto finished = *broadcast();

  ASSERT_EQ(SERVERS, finished);
}
