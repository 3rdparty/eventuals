#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/sequence.h"
#include "stout/task.h"

#include "stout/eventuals/grpc/client.h"

#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto
#include "examples/protos/keyvaluestore.grpc.pb.h"

#include "test/test.h"

using stout::borrowable;
using stout::Notification;
using stout::Sequence;

using stout::grpc::ServerBuilder;
using stout::grpc::ServerCallStatus;
using stout::grpc::Stream;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Handler;

using testing::MockFunction;

TEST_F(StoutEventualsGrpcTest, Streaming)
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

  // Mock for server write callbacks.
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
            const size_t TIMES = 3;
            EXPECT_CALL(write, Call(true))
              .Times(TIMES);
            for (size_t i = 0; i < TIMES; i++) {
              keyvaluestore::Response response;
              response.set_value(stringify(i + 3));
              auto status = call->Write(response, write.AsStdFunction());
              EXPECT_EQ(ServerCallStatus::Ok, status);
            }
            call->Finish(grpc::Status::OK);
          }
        },
        [&](auto*, bool cancelled) {
          done.Notify(cancelled);
        });

  ASSERT_TRUE(serve.ok());

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
         .ready(Sequence()
                .Once([](auto& call) {
                  keyvaluestore::Request request;
                  request.set_key("1");
                  call.Write(request);
                })
                .Once([](auto& call) {
                  keyvaluestore::Request request;
                  request.set_key("2");
                  call.WriteLast(request);
                }))
         .body(Sequence()
               .Once([](auto& call, auto&& response) {
                 EXPECT_EQ("1", response->value());
               })
               .Once([](auto& call, auto&& response) {
                 EXPECT_EQ("2", response->value());
               })
               .Once([](auto& call, auto&& response) {
                 EXPECT_EQ("3", response->value());
               })
               .Once([](auto& call, auto&& response) {
                 EXPECT_EQ("4", response->value());
               })
               .Once([](auto& call, auto&& response) {
                 EXPECT_EQ("5", response->value());
               })
               .Once([](auto& call, auto&& response) {
                 EXPECT_FALSE(response);
               })));
  };

  auto status = *call();

  ASSERT_TRUE(status.ok()) << status.error_message();

  ASSERT_FALSE(done.Wait());
}
