#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/promisify.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/grpc/test.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using testing::ThrowsMessage;

TEST(AcceptTest, ServeValidate) {
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", ::grpc::InsecureServerCredentials());

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 keyvaluestore::Request,
                 Stream<keyvaluestore::Response>>("GetValues")
          >> Head();
    };

    try {
      *serve();
    } catch (const RuntimeError& error) {
      EXPECT_EQ(error.what(), "Method has streaming requests");
    }
  }

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 Stream<keyvaluestore::Request>,
                 keyvaluestore::Response>("GetValues")
          >> Head();
    };

    try {
      *serve();
    } catch (const RuntimeError& error) {
      EXPECT_EQ(error.what(), "Method has streaming responses");
    }
  }

  {
    auto serve = [&]() {
      return server->Accept<Greeter, Stream<HelloRequest>, HelloReply>(
                 "SayHello")
          >> Head();
    };

    try {
      *serve();
    } catch (const RuntimeError& error) {
      EXPECT_EQ(error.what(), "Method does not have streaming requests");
    }
  }

  {
    auto serve = [&]() {
      return server->Accept<Greeter, HelloRequest, Stream<HelloReply>>(
                 "SayHello")
          >> Head();
    };

    try {
      *serve();
    } catch (const RuntimeError& error) {
      EXPECT_EQ(error.what(), "Method does not have streaming responses");
    }
  }

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 Stream<HelloRequest>,
                 Stream<keyvaluestore::Response>>("GetValues")
          >> Head();
    };

    try {
      *serve();
    } catch (const RuntimeError& error) {
      EXPECT_EQ(
          error.what(),
          "Method does not have requests of type helloworld.HelloRequest");
    }
  }

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 Stream<keyvaluestore::Request>,
                 Stream<HelloReply>>("GetValues")
          >> Head();
    };

    try {
      *serve();
    } catch (const RuntimeError& error) {
      EXPECT_EQ(
          error.what(),
          "Method does not have responses of type helloworld.HelloReply");
    }
  }
}

} // namespace
} // namespace eventuals::grpc::test
