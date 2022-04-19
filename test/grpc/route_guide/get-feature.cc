
#include <string>

#include "eventuals/finally.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "helper.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"
#include "test/grpc/route_guide/route-guide-utilities.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::CompletionPool;
using eventuals::grpc::ServerBuilder;
using grpc::Status;

using stout::Borrowable;

using eventuals::Finally;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;

class RouteGuideGetFeatureClient : public RouteGuideBaseClient {
 public:
  RouteGuideGetFeatureClient(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : RouteGuideBaseClient(target, credentials, std::move(pool), db) {}

  auto GetFeature();

 private:
  auto GetOneFeature(Point&& point) {
    return client_.Call<RouteGuide, Point, Feature>("GetFeature")
        | Then(Let([this, point = std::move(point)](auto& call) {
             return call.Writer().WriteLast(point)
                 | call.Reader().Read()
                 | Head()
                 | Finally(Let([&](auto& feature) {
                      return call.Finish()
                          | Then([&](Status&& status) {
                               CHECK(status.ok()) << status.error_code()
                                                  << ": "
                                                  << status.error_message();
                               CHECK(feature) << "GetFeature rpc failed";

                               CHECK(feature->has_location())
                                   << "Server returns incomplete feature";

                               auto latitude = feature->location().latitude();
                               auto longitude =
                                   feature->location().longitude();

                               if (feature->name().empty()) {
                                 EXPECT_EQ(
                                     latitude / kCoordFactor_,
                                     0);
                                 EXPECT_EQ(
                                     longitude / kCoordFactor_,
                                     0);
                               } else {
                                 EXPECT_EQ(
                                     feature->name(),
                                     "BerkshireValleyManagementAreaTrail,"
                                     "Jefferson,NJ,USA");
                                 EXPECT_NEAR(
                                     latitude / kCoordFactor_,
                                     40.9146,
                                     0.1);
                                 EXPECT_NEAR(
                                     longitude / kCoordFactor_,
                                     -74.6189,
                                     0.1);
                               }
                               return true;
                             });
                    }));
           }));
  }

  const float kCoordFactor_ = 10000000.0;
};

auto RouteGuideGetFeatureClient::GetFeature() {
  return GetOneFeature(MakePoint(409146138, -746188906))
      | Then([this](bool) {
           return GetOneFeature(MakePoint(0, 0));
         });
}

TEST(RouteGuideTest, GetFeatureTest) {
  std::string db_path("test/grpc/route_guide/route_guide_db.json");

  std::string db;
  routeguide::GetDbFileContent(db_path, db);

  RouteGuideImpl service(db);

  ServerBuilder builder;
  std::string server_address("localhost:8888");

  builder.AddListeningPort(
      server_address,
      grpc::InsecureServerCredentials());

  builder.RegisterService(&service);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  Borrowable<CompletionPool> pool;

  RouteGuideGetFeatureClient guide(
      server_address,
      grpc::InsecureChannelCredentials(),
      pool.Borrow(),
      db);

  EXPECT_TRUE(*guide.GetFeature());
}
