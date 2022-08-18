#include <algorithm>
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-test.h"

using grpc::Status;

using eventuals::Finally;
using eventuals::operator*;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;

using eventuals::grpc::Stream;

TEST_F(RouteGuideTest, ListFeaturesTest) {
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

  routeguide::Rectangle rect;
  rect.mutable_lo()->set_latitude(bottom_);
  rect.mutable_lo()->set_longitude(left_);
  rect.mutable_hi()->set_latitude(top_);
  rect.mutable_hi()->set_longitude(right_);

  auto e = [&]() {
    return client->ListFeatures()
        >> Then(Let([&](auto& call) {
             return call.Writer().WriteLast(std::move(rect))
                 >> Foreach(
                        call.Reader().Read(),
                        [this](Feature&& feature) {
                          auto latitude = feature.location().latitude();
                          auto longitude = feature.location().longitude();

                          auto& expected_feature =
                              filtered_[current_++];

                          EXPECT_EQ(
                              expected_feature.location().latitude(),
                              latitude);

                          EXPECT_EQ(
                              expected_feature.location().longitude(),
                              longitude);

                          EXPECT_EQ(
                              expected_feature.name(),
                              feature.name());
                        })
                 >> Finally([&call](auto) {
                      return call.Finish();
                    })
                 >> Then([](Status&& status) {
                      CHECK(status.ok()) << status.error_code()
                                         << ": " << status.error_message();
                    });
           }))
        >> Then([this]() {
             CHECK_EQ(current_, filtered_.size());
           });
  };

  *e();
}
