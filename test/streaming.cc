#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/grpc/client.h"
#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto
#include "examples/protos/keyvaluestore.grpc.pb.h"

#include "test.h"

using stout::Notification;

using stout::grpc::Client;
using stout::grpc::ClientCallStatus;
using stout::grpc::ServerBuilder;
using stout::grpc::ServerCallStatus;
using stout::grpc::Stream;

using testing::MockFunction;


TEST_F(StoutGrpcTest, Streaming)
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

  // Mock for both server and client write callbacks.
  MockFunction<void(bool)> write;

  Notification<bool> done;

  auto serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        [&](auto* call, auto&& request) {
          if (request) {
            keyvaluestore::Response response;
            response.set_value(request->key());
            EXPECT_CALL(write, Call(true))
              .Times(1);
            auto status = call->Write(response, write.AsStdFunction());
            EXPECT_EQ(ServerCallStatus::Ok, status);
          } else {
            call->Finish(grpc::Status::OK);
          }
        },
        [&](auto*, bool cancelled) {
          done.Notify(cancelled);
        });

  ASSERT_TRUE(serve.ok());

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials());

  keyvaluestore::Request request;
  request.set_key("0");

  Notification<grpc::Status> finished;

  auto status = client.Call<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        &request,
        [&](auto* call, auto&& response) {
          if (response) {
            EXPECT_EQ(request.key(), response->value());
            if (request.key() == "1") {
              EXPECT_EQ(ClientCallStatus::Ok, call->WritesDoneAndFinish());
            } else {
              request.set_key("1");
              EXPECT_CALL(write, Call(true))
                .Times(1);
              auto status = call->Write(request, write.AsStdFunction());
              EXPECT_EQ(ClientCallStatus::Ok, status);
            }
          }
        },
        [&](auto*, const grpc::Status& status) {
          finished.Notify(status);
        });

  ASSERT_TRUE(status.ok());

  ASSERT_TRUE(finished.Wait().ok());

  ASSERT_FALSE(done.Wait());
}
