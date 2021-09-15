#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/grpc/client.h"
#include "stout/grpc/server.h"
#include "stout/head.h"
#include "stout/sequence.h"
#include "stout/then.h"
#include "test/test.h"

using stout::Borrowable;
using stout::Sequence;

using stout::eventuals::Head;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;
using stout::eventuals::grpc::Stream;

template <class T>
void test_client_behavior(T&& handler) {
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

  auto serve = [&]() {
    return server->Accept<
               Stream<keyvaluestore::Request>,
               Stream<keyvaluestore::Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        | Head()
        | Then([](auto&& context) {
             return Server::Handler(std::move(context))
                 .body([&](auto& call, auto&& request) {
                   if (request) {
                     keyvaluestore::Response response;
                     response.set_value(request->key());
                     call.Write(response);
                   } else {
                     for (size_t i = 0; i < 3; i++) {
                       keyvaluestore::Response response;
                       response.set_value(stringify(i + 10));
                       call.Write(response);
                     }
                     call.Finish(grpc::Status::OK);
                   }
                 });
           });
  };

  auto [cancelled, k] = Terminate(serve());

  k.Start();

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<
               Stream<keyvaluestore::Request>,
               Stream<keyvaluestore::Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        | handler;
  };

  auto status = *call();

  EXPECT_TRUE(status.ok()) << status.error_message();

  EXPECT_FALSE(cancelled.get());
}

TEST_F(StoutGrpcTest, Streaming_FinishAfterReply) {
  test_client_behavior(Client::Handler()
                           .ready([](auto& call) {
                             keyvaluestore::Request request;
                             request.set_key("1");
                             call.Write(request);
                           })
                           .body(Sequence()
                                     .Once([](auto& call, auto&& response) {
                                       EXPECT_EQ("1", response->value());
                                       keyvaluestore::Request request;
                                       request.set_key("2");
                                       call.WriteLast(request);
                                     })
                                     .Once([](auto& call, auto&& response) {
                                       EXPECT_EQ("2", response->value());
                                     })
                                     .Once([](auto& call, auto&& response) {
                                       EXPECT_EQ("10", response->value());
                                     })
                                     .Once([](auto& call, auto&& response) {
                                       EXPECT_EQ("11", response->value());
                                     })
                                     .Once([](auto& call, auto&& response) {
                                       EXPECT_EQ("12", response->value());
                                     })
                                     .Once([](auto& call, auto&& response) {
                                       EXPECT_FALSE(response);
                                     })));
}
