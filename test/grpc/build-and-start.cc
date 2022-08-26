#include "eventuals/grpc/server.hh"
#include "gtest/gtest.h"
#include "test/grpc/test.hh"

namespace eventuals::grpc::test {
namespace {

TEST(BuildAndStartTest, Success) {
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", ::grpc::InsecureServerCredentials());

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;
  ASSERT_TRUE(build.server);
}

} // namespace
} // namespace eventuals::grpc::test
