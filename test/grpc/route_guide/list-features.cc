
#include <algorithm>
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
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

using eventuals::grpc::ServerBuilder;
using grpc::Status;

using stout::Borrowable;

using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Stream;

class RouteGuideListFeaturesClient : public RouteGuideBaseClient {
 public:
  RouteGuideListFeaturesClient(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : RouteGuideBaseClient(target, credentials, std::move(pool), db) {
    std::copy_if(
        feature_list_.begin(),
        feature_list_.end(),
        std::back_inserter(filtered_),
        [this](const auto& feature) {
          return feature.location().longitude() >= left_
              && feature.location().longitude() <= right_
              && feature.location().latitude() >= bottom_
              && feature.location().latitude() <= top_;
        });
  }

  auto ListFeatures() {
    return client_.Call<RouteGuide, Rectangle, Stream<Feature>>("ListFeatures")
        | Then(Let([this](auto& call) {
             routeguide::Rectangle rect;

             rect.mutable_lo()->set_latitude(bottom_);
             rect.mutable_lo()->set_longitude(left_);
             rect.mutable_hi()->set_latitude(top_);
             rect.mutable_hi()->set_longitude(right_);

             return call.Writer().WriteLast(rect)
                 | Foreach(
                        call.Reader().Read(),
                        ([&](Feature&& feature) {
                          auto latitude = feature.location().latitude();
                          auto longitude = feature.location().longitude();

                          auto& expected_feature = filtered_[current_++];

                          EXPECT_EQ(
                              expected_feature.location().latitude(),
                              latitude);

                          EXPECT_EQ(
                              expected_feature.location().longitude(),
                              longitude);

                          EXPECT_EQ(
                              expected_feature.name(),
                              feature.name());
                        }))
                 | Finally([&](auto) {
                      return call.Finish();
                    })
                 | Then([this](Status&& status) {
                      CHECK(status.ok()) << status.error_code()
                                         << ": " << status.error_message();
                      CHECK_EQ(current_, filtered_.size());

                      return true;
                    });
           }));
  }

 private:
  std::vector<Feature> filtered_;

  const size_t left_ = -750000000;
  const size_t right_ = -730000000;
  const size_t top_ = 420000000;
  const size_t bottom_ = 400000000;

  size_t current_ = 0;
};

TEST(RouteGuideTest, ListFeaturesTest) {
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

  RouteGuideListFeaturesClient guide(
      server_address,
      grpc::InsecureChannelCredentials(),
      pool.Borrow(),
      db);

  EXPECT_TRUE(*guide.ListFeatures());
}
