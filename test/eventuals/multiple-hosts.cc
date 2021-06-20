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

TEST_F(StoutEventualsGrpcTest, MultipleHosts)
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

  Notification<bool> berkeley;
  
  auto serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        "cs.berkeley.edu",
        [&](auto&& call) {
          call->Finish(grpc::Status::OK);
          call->OnDone([&](auto*, bool cancelled) {
            berkeley.Notify(cancelled);
          });
        });

  ASSERT_TRUE(serve.ok());

  Notification<bool> washington;

  serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        "cs.washington.edu",
        [&](auto&& call) {
          call->Finish(grpc::Status::OK);
          call->OnDone([&](auto*, bool cancelled) {
            washington.Notify(cancelled);
          });
        });

  ASSERT_TRUE(serve.ok());

  borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.borrow());

  auto call1 = [&]() {
    return client.Call<
      Stream<keyvaluestore::Request>,
      Stream<keyvaluestore::Response>>(
          "keyvaluestore.KeyValueStore.GetValues",
          "cs.berkeley.edu")
      | (Handler<grpc::Status>()
         .ready([](auto& call) {
           call.WritesDone();
         }));
  };

  auto status = *call1();

  ASSERT_TRUE(status.ok());

  ASSERT_FALSE(berkeley.Wait());

  auto call2 = [&]() {
    return client.Call<
      Stream<keyvaluestore::Request>,
      Stream<keyvaluestore::Response>>(
          "keyvaluestore.KeyValueStore.GetValues",
          "cs.washington.edu")
      | (Handler<grpc::Status>()
         .ready([](auto& call) {
           call.WritesDone();
         }));
  };

  status = *call2();

  ASSERT_TRUE(status.ok());

  ASSERT_FALSE(washington.Wait());
}
