
#include <algorithm>
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using grpc::Status;


using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;

using eventuals::grpc::Stream;

auto RouteGuideClient::ListFeatures() {
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

TEST_F(RouteGuideTest, ListFeaturesTest) {
  EXPECT_TRUE(*guide.ListFeatures());
}
