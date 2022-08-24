#include <algorithm>
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-test.h"

using eventuals::Finally;
using eventuals::operator*;
using eventuals::Foreach;
using eventuals::Let;
using eventuals::Then;
using routeguide::Feature;

TEST_F(RouteGuideTest, ListFeaturesTest) {
  const int32_t left = -750000000;
  const int32_t right = -730000000;
  const int32_t top = 420000000;
  const int32_t bottom = 400000000;

  std::vector<Feature> expected_features;
  size_t current = 0;

  std::copy_if(
      feature_list.begin(),
      feature_list.end(),
      std::back_inserter(expected_features),
      [&](const auto& feature) {
        return feature.location().longitude() >= left
            && feature.location().longitude() <= right
            && feature.location().latitude() >= bottom
            && feature.location().latitude() <= top;
      });

  routeguide::Rectangle rect;
  rect.mutable_lo()->set_latitude(bottom);
  rect.mutable_lo()->set_longitude(left);
  rect.mutable_hi()->set_latitude(top);
  rect.mutable_hi()->set_longitude(right);

  auto client = CreateClient();

  auto e = [&]() {
    return client.ListFeatures()
        >> Then(Let([&](auto& call) {
             return call.Writer().WriteLast(std::move(rect))
                 >> Foreach(
                        call.Reader().Read(),
                        [&](Feature&& feature) {
                          auto& expected_feature =
                              expected_features[current++];

                          EXPECT_EQ(
                              expected_feature.location().latitude(),
                              feature.location().latitude());

                          EXPECT_EQ(
                              expected_feature.location().longitude(),
                              feature.location().longitude());

                          EXPECT_EQ(
                              expected_feature.name(),
                              feature.name());
                        })
                 >> Finally([&call](auto) {
                      return call.Finish();
                    })
                 >> Then([](::grpc::Status&& status) {
                      EXPECT_TRUE(status.ok())
                          << status.error_code() << ": "
                          << status.error_message();
                    });
           }))
        >> Then([&]() {
             EXPECT_EQ(current, expected_features.size());
           });
  };

  *e();
}
