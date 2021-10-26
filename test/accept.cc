#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using eventuals::Head;

using eventuals::grpc::ServerBuilder;
using eventuals::grpc::Stream;

#define EXPECT_THROW_WHAT(_expression_, _what_)                        \
  [&]() {                                                              \
    try {                                                              \
      (_expression_);                                                  \
    } catch (const std::exception& e) {                                \
      if (std::string(e.what()) != std::string(_what_)) {              \
        FAIL()                                                         \
            << "std::exception::what() is '" << e.what()               \
            << "' which does not match '" << (_what_) << "'";          \
      } else {                                                         \
        return;                                                        \
      }                                                                \
    } catch (...) {                                                    \
      FAIL()                                                           \
          << "caught exception does not inherit from std::exception "; \
    }                                                                  \
    FAIL() << "no exception thrown ";                                  \
  }()


TEST_F(EventualsGrpcTest, ServeValidate) {
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials());

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
