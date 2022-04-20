#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/expect-throw-what.h"
#include "test/grpc/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

namespace eventuals::grpc {
namespace {
TEST_F(EventualsGrpcTest, ServeValidate) {
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", ::grpc::InsecureServerCredentials());

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok());

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 keyvaluestore::Request,
                 Stream<keyvaluestore::Response>>("GetValues")
          | Head();
    };

    EXPECT_THROW_WHAT(*serve(), "Method has streaming requests");
  }

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 Stream<keyvaluestore::Request>,
                 keyvaluestore::Response>("GetValues")
          | Head();
    };

    EXPECT_THROW_WHAT(*serve(), "Method has streaming responses");
  }

  {
    auto serve = [&]() {
      return server->Accept<Greeter, Stream<HelloRequest>, HelloReply>(
                 "SayHello")
          | Head();
    };

    EXPECT_THROW_WHAT(*serve(), "Method does not have streaming requests");
  }

  {
    auto serve = [&]() {
      return server->Accept<Greeter, HelloRequest, Stream<HelloReply>>(
                 "SayHello")
          | Head();
    };

    EXPECT_THROW_WHAT(*serve(), "Method does not have streaming responses");
  }

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 Stream<HelloRequest>,
                 Stream<keyvaluestore::Response>>("GetValues")
          | Head();
    };

    EXPECT_THROW_WHAT(
        *serve(),
        "Method does not have requests of type helloworld.HelloRequest");
  }

  {
    auto serve = [&]() {
      return server->Accept<
                 keyvaluestore::KeyValueStore,
                 Stream<keyvaluestore::Request>,
                 Stream<HelloReply>>("GetValues")
          | Head();
    };

    EXPECT_THROW_WHAT(
        *serve(),
        "Method does not have responses of type helloworld.HelloReply");
  }
}
} // namespace
} // namespace eventuals::grpc
