#include "gtest/gtest.h"

#include "stout/grpc/client.h"
#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto
#include "examples/protos/keyvaluestore.grpc.pb.h"

#include "test.h"

using stout::Notification;

using stout::grpc::Client;
using stout::grpc::ServerBuilder;
using stout::grpc::Stream;


TEST_F(StoutGrpcTest, ClientDeathTest)
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
    Client client(
        "0.0.0.0:" + stringify(port),
        grpc::InsecureChannelCredentials());

    Notification<bool> started;

    auto status = client.Call<
      Stream<keyvaluestore::Request>,
      Stream<keyvaluestore::Response>>(
          "keyvaluestore.KeyValueStore.GetValues",
          [&](auto&& call, bool ok) {
            started.Notify(ok);
          });

    ASSERT_TRUE(status.ok());

    ASSERT_TRUE(started.Wait());

    exit(1);
  };

  ASSERT_DEATH(client(), "");

  ASSERT_TRUE(done.Wait());
}
