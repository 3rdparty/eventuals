#include <grpcpp/channel.h>

#include "route-guide-eventuals-client.h"
#include "route-guide-eventuals-server.h"
#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route_guide.eventuals.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::ServerBuilder;

////////////////////////////////////////////////////////////////////////

void RouteGuideImpl::ParseDb(const std::string& db) {
  routeguide::ParseDb(db, &feature_list_);
}

Feature RouteGuideImpl::GetFeature(
    grpc::ServerContext* context,
    Point&& point) {
  Feature feature;
  feature.set_name(GetFeatureName(point, feature_list_));
  feature.mutable_location()->CopyFrom(point);
  return feature;
}

////////////////////////////////////////////////////////////////////////

int RouteGuideTest::GetPointsCount() {
  return kPoints_;
}

void RouteGuideTest::SetDb(const std::string& db) {
  routeguide::ParseDb(db, &feature_list_);
}

void RouteGuideTest::SetUp() {
  std::string db;
  routeguide::GetDbFileContent(db_path_, db);

  service_.ParseDb(db);

  ServerBuilder builder;
  builder.AddListeningPort(
      server_address_ + ":0",
      ::grpc::InsecureServerCredentials(),
      &port_);
  builder.RegisterService(&service_);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;
  server_.swap(build.server);
  ASSERT_TRUE(server_);

  client.emplace(
      server_address_ + ":" + std::to_string(port_),
      ::grpc::InsecureChannelCredentials(),
      pool_.Borrow());

  SetDb(db);
}

////////////////////////////////////////////////////////////////////////
