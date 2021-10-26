#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, Unimplemented) {
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

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | (Client::Handler()
               .body([](auto& call, auto&& response) {
                 EXPECT_FALSE(response);
                 call.WritesDone();
               }));
  };

  auto status = *call();

  ASSERT_EQ(grpc::UNIMPLEMENTED, status.error_code());
}
