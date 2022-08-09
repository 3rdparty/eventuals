
#include <algorithm>
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

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
      guide.feature_list_.begin(),
      guide.feature_list_.end(),
      std::back_inserter(guide.filtered_),
      [this](const auto& feature) {
        return feature.location().longitude() >= guide.left_
            && feature.location().longitude() <= guide.right_
            && feature.location().latitude() >= guide.bottom_
            && feature.location().latitude() <= guide.top_;
      });

  routeguide::Rectangle rect;
  rect.mutable_lo()->set_latitude(guide.bottom_);
  rect.mutable_lo()->set_longitude(guide.left_);
  rect.mutable_hi()->set_latitude(guide.top_);
  rect.mutable_hi()->set_longitude(guide.right_);

  auto e = [&]() {
    return guide.ListFeatures()
        >> Then(Let([&](auto& call) {
             return call.Writer().WriteLast(std::move(rect))
                 >> Foreach(
                        call.Reader().Read(),
                        [&](Feature&& feature) {
                          auto latitude = feature.location().latitude();
                          auto longitude = feature.location().longitude();

                          auto& expected_feature =
                              guide.filtered_[guide.current_++];

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
                 >> Finally([&](auto) {
                      return call.Finish();
                    })
                 >> Then([](Status&& status) {
                      CHECK(status.ok()) << status.error_code()
                                         << ": " << status.error_message();
                    });
           }))
        >> Then([&]() {
             CHECK_EQ(guide.current_, guide.filtered_.size());
             return true;
           });
  };

  EXPECT_TRUE(*e());
}
