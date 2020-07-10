#include "gtest/gtest.h"

#include "stout/grpc/server.h"

using stout::grpc::ServerBuilder;


TEST(GrpcTest, BuildAndStart)
{
  ServerBuilder builder;

  builder.AddListeningPort("0.0.0.0:0", grpc::InsecureServerCredentials());

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok());
  ASSERT_TRUE(build.server);
}
