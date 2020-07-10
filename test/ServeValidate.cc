#include "gtest/gtest.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/helloworld.proto
#include "examples/protos/helloworld.grpc.pb.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto
#include "examples/protos/keyvaluestore.grpc.pb.h"

#include "stout/grpc/server.h"

using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

using stout::grpc::ServerBuilder;
using stout::grpc::Stream;


TEST(GrpcTest, ServeValidate)
{
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials());

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok());

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  auto serve = server->Serve<
    keyvaluestore::Request,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        [](auto&& call) {});

  ASSERT_FALSE(serve.ok());
  EXPECT_EQ("Method has streaming requests", serve.error());

  serve = server->Serve<
    Stream<keyvaluestore::Request>,
    keyvaluestore::Response>(
        "keyvaluestore.KeyValueStore.GetValues",
        [](auto&& call) {});

  ASSERT_FALSE(serve.ok());
  EXPECT_EQ("Method has streaming responses", serve.error());

  serve = server->Serve<Greeter, Stream<HelloRequest>, HelloReply>(
      "SayHello",
      [](auto&& call) {});

  ASSERT_FALSE(serve.ok());
  EXPECT_EQ("Method does not have streaming requests", serve.error());

  serve = server->Serve<Greeter, HelloRequest, Stream<HelloReply>>(
      "SayHello",
      [](auto&& call) {});

  ASSERT_FALSE(serve.ok());
  EXPECT_EQ("Method does not have streaming responses", serve.error());

  serve = server->Serve<
    Stream<HelloRequest>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        [](auto&& call) {});

  ASSERT_FALSE(serve.ok());
  EXPECT_EQ(
      "Method does not have requests of type helloworld.HelloRequest",
      serve.error());

  serve = server->Serve<
    Stream<keyvaluestore::Request>,
    Stream<HelloReply>>(
        "keyvaluestore.KeyValueStore.GetValues",
        [](auto&& call) {});

  ASSERT_FALSE(serve.ok());
  EXPECT_EQ(
      "Method does not have responses of type helloworld.HelloReply",
      serve.error());
}
