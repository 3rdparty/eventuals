#include "gtest/gtest.h"

#include "stout/task.h"

#include "stout/eventuals/grpc/client.h"

#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto
#include "examples/protos/keyvaluestore.grpc.pb.h"

#include "test/test.h"

using stout::borrowable;
using stout::Notification;

using stout::grpc::ServerBuilder;
using stout::grpc::Stream;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Handler;

TEST_F(StoutEventualsGrpcTest, ClientDeathTest)
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
  
  auto serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        [&](auto&& call) {
          call->OnDone([&](auto* call, bool cancelled) {
            done.Notify(cancelled);
          });
        });

  ASSERT_TRUE(serve.ok());

  auto client = [&]() {
    borrowable<CompletionPool> pool;

    Client client(
        "0.0.0.0:" + stringify(port),
        grpc::InsecureChannelCredentials(),
        pool.borrow());

    auto call = [&]() {
      return client.Call<
        Stream<keyvaluestore::Request>,
        Stream<keyvaluestore::Response>>(
            "keyvaluestore.KeyValueStore.GetValues")
        | (Handler<grpc::Status>()
           .ready([](auto&) {
             exit(1);
           }));
    };

    *call();
  };

  ASSERT_DEATH(client(), "");

  ASSERT_TRUE(done.Wait());
}
