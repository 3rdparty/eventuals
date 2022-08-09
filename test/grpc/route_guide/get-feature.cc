
#include <string>

#include "eventuals/finally.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using grpc::Status;

using eventuals::Finally;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;
using eventuals::operator*;

auto RouteGuideTest::GetFeatureTest() {
  return client->GetFeature(MakePoint(409146138, -746188906))
      >> Then([this](auto&&) {
           return client->GetFeature(MakePoint(0, 0));
         });
}

TEST_F(RouteGuideTest, GetFeatureTest) {
  auto e = [this]() {
    return GetFeatureTest()
        >> Then([](auto&& feature) {
             CHECK(feature.has_location())
                 << "Server returns incomplete feature";

             auto latitude = feature.location().latitude();
             auto longitude =
                 feature.location().longitude();

             if (feature.name().empty()) {
               EXPECT_EQ(
                   latitude / 10000000.0,
                   0);
               EXPECT_EQ(
                   longitude / 10000000.0,
                   0);
             } else {
               EXPECT_EQ(
                   feature.name(),
                   "BerkshireValleyManagementAreaTrail,"
                   "Jefferson,NJ,USA");
               EXPECT_NEAR(
                   latitude / 10000000.0,
                   40.9146,
                   0.1);
               EXPECT_NEAR(
                   longitude / 10000000.0,
                   -74.6189,
                   0.1);
             }
             return true;
           });
  };

  EXPECT_TRUE(*e());
}
