#include "route-guide-test.h"

#include <grpcpp/channel.h>

#include "route-guide-eventuals-server.h"
#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route_guide.eventuals.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::ServerBuilder;

////////////////////////////////////////////////////////////////////////

RouteGuideTest::RouteGuideTest()
  : feature_list([]() {
      std::string db = routeguide::GetDbFileContent();
      std::vector<routeguide::Feature> temp;
      routeguide::ParseDb(db, &temp);
      return temp;
    }()),
    service_(feature_list) {}

routeguide::eventuals::RouteGuide::Client RouteGuideTest::CreateClient() {
  return server_->client<routeguide::eventuals::RouteGuide::Client>(
      pool_.Borrow());
}

void RouteGuideTest::SetUp() {
  const std::string server_address_ = "0.0.0.0";
  int port = 0;

  ServerBuilder builder;
  builder.AddListeningPort(
      server_address_ + ":0",
      ::grpc::InsecureServerCredentials(),
      &port);
  builder.RegisterService(&service_);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;
  server_.swap(build.server);
  ASSERT_TRUE(server_);
}

////////////////////////////////////////////////////////////////////////
