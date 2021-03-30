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


TEST_F(StoutGrpcTest, MultipleHosts)
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

  Notification<bool> berkeley_done;
  
  auto serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        "cs.berkeley.edu",
        [&](auto&& call) {
          call->Finish(grpc::Status::OK);
          call->OnDone([&](auto*, bool cancelled) {
            berkeley_done.Notify(cancelled);
          });
        });

  ASSERT_TRUE(serve.ok());

  Notification<bool> washington_done;

  serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        "cs.washington.edu",
        [&](auto&& call) {
          call->Finish(grpc::Status::OK);
          call->OnDone([&](auto*, bool cancelled) {
            washington_done.Notify(cancelled);
          });
        });

  ASSERT_TRUE(serve.ok());

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials());

  Notification<grpc::Status> berkeley_finished;

  auto status = client.Call<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        "cs.berkeley.edu",
        [&](auto&& call, bool ok) {
          EXPECT_TRUE(ok);
          call->Finish([&](auto*, const grpc::Status& status) {
            berkeley_finished.Notify(status);
          });
        });

  ASSERT_TRUE(status.ok());

  ASSERT_TRUE(berkeley_finished.Wait().ok());

  ASSERT_FALSE(berkeley_done.Wait());

  Notification<grpc::Status> washington_finished;

  status = client.Call<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        "cs.washington.edu",
        [&](auto&& call, bool ok) {
          EXPECT_TRUE(ok);
          call->Finish([&](auto*, const grpc::Status& status) {
            washington_finished.Notify(status);
          });
        });

  ASSERT_TRUE(status.ok());

  ASSERT_TRUE(washington_finished.Wait().ok());

  ASSERT_FALSE(washington_done.Wait());
}
