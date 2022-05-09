
#include <string>

#include "eventuals/finally.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using grpc::Status;

using eventuals::Finally;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;

auto RouteGuideClient::GetOneFeature(Point&& point) {
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

auto RouteGuideClient::GetFeature() {
  return GetOneFeature(MakePoint(409146138, -746188906))
      | Then([this](bool) {
           return GetOneFeature(MakePoint(0, 0));
         });
}

TEST_F(RouteGuideTest, GetFeatureTest) {
  EXPECT_TRUE(*guide.GetFeature());
}
