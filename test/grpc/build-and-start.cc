#include "eventuals/grpc/server.h"
#include "gtest/gtest.h"
#include "test/grpc/test.h"

namespace eventuals::grpc {
namespace {
TEST_F(EventualsGrpcTest, BuildAndStart) {
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", ::grpc::InsecureServerCredentials());

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok());
  ASSERT_TRUE(build.server);
}
} // namespace
} // namespace eventuals::grpc
