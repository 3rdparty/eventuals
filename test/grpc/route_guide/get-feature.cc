#include <string>

#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-test.h"

using eventuals::Then;
using eventuals::operator*;

TEST_F(RouteGuideTest, GetFeatureTest) {
  auto client = CreateClient();
  auto e = [&]() {
    return client.GetFeature(MakePoint(409146138, -746188906))
        >> Then([&](auto&& feature) {
             EXPECT_TRUE(feature.has_location())
                 << "Server returns incomplete feature";
             EXPECT_EQ(
                 feature.name(),
                 "BerkshireValleyManagementAreaTrail,"
                 "Jefferson,NJ,USA");
             EXPECT_NEAR(
                 feature.location().latitude() / 10000000.0,
                 40.9146,
                 0.1);
             EXPECT_NEAR(
                 feature.location().longitude() / 10000000.0,
                 -74.6189,
                 0.1);

             return client.GetFeature(MakePoint(0, 0));
           })
        >> Then([](auto&& feature) {
             EXPECT_TRUE(feature.has_location())
                 << "Server returns incomplete feature";
             EXPECT_EQ(feature.name(), "");
             EXPECT_EQ(feature.location().latitude() / 10000000.0, 0);
             EXPECT_EQ(feature.location().longitude() / 10000000.0, 0);
           });
  };

  *e();
}
