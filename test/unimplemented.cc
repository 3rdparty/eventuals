#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/let.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Let;

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
      "0.0.0.0:" + std::to_string(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then(Let([](auto& call) {
             return call.Finish();
           }));
  };

  auto status = *call();

  ASSERT_EQ(grpc::UNIMPLEMENTED, status.error_code());
}
