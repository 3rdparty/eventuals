#include <grpcpp/channel.h>

#include "route-guide-eventuals-client.h"
#include "route-guide-eventuals-server.h"
#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"
#include "test/grpc/route_guide/route_guide_generated/route_guide.eventuals.h"

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

RouteGuideClient::RouteGuideClient(
    const std::string& target,
    const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
    stout::borrowed_ptr<CompletionPool> pool,
    const std::string& db)
  : client_(target, credentials, std::move(pool)) {
}

void RouteGuideClient::SetDb(const std::string& db) {
  routeguide::ParseDb(db, &feature_list_);
}

////////////////////////////////////////////////////////////////////////

RouteGuideTest::RouteGuideTest()
  : guide(
      server_address_,
      ::grpc::InsecureChannelCredentials(),
      pool_.Borrow(),
      "") {}

void RouteGuideTest::SetUp() {
  std::string db;
  routeguide::GetDbFileContent(db_path_, db);

  service_.ParseDb(db);

  ServerBuilder builder;
  builder.AddListeningPort(
      server_address_,
      ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service_);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;
  server_.swap(build.server);
  ASSERT_TRUE(server_);

  guide.SetDb(db);
}

////////////////////////////////////////////////////////////////////////
