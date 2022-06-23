
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/range.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using eventuals::grpc::Stream;
using grpc::Status;

using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Range;
using eventuals::Then;

using routeguide::Point;

TEST_F(RouteGuideTest, RecordRouteTest) {
  auto e = [&]() {
    return guide.RecordRoute([&]() {
      std::vector<Point> data(guide.GetPointsCount());

      for (size_t i = 0; i < guide.GetPointsCount(); ++i) {
        data[i] = guide.feature_list_[i].location();
      }

      return Iterate(std::move(data));
    })
        | Then([&](auto&& summary) {
             CHECK_EQ(summary.point_count(), guide.GetPointsCount());
             CHECK_EQ(summary.feature_count(), guide.GetPointsCount());
             CHECK_EQ(summary.distance(), 675412);

             return true;
           });
  };

  EXPECT_TRUE(*e());
}
