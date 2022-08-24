#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-test.h"

using eventuals::Finally;
using eventuals::Foreach;
using eventuals::operator*;
using eventuals::Head;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Then;

using routeguide::Point;

TEST_F(RouteGuideTest, RecordRouteTest) {
  const int points = 10;
  std::vector<Point> requests(points);
  EXPECT_LE(points, feature_list.size());

  for (size_t i = 0; i < points; ++i) {
    requests[i] = feature_list[i].location();
  }

  auto client = CreateClient();

  auto e = [&]() {
    return client.RecordRoute()
        >> Then(Let([&requests](auto& call) mutable {
             return Foreach(
                        Iterate(std::move(requests)),
                        ([&call](auto&& request) {
                          return call.Writer().Write(request);
                        }))
                 >> call.WritesDone()
                 >> call.Reader().Read()
                 >> Head()
                 >> Finally(Let([&call](auto& response) {
                      return call.Finish()
                          >> Then([&](::grpc::Status&& status) {
                               EXPECT_TRUE(status.ok())
                                   << status.error_code()
                                   << ": " << status.error_message();
                               EXPECT_TRUE(response);
                               return std::move(response);
                             });
                    }));
           }))
        >> Then([&](auto&& summary) {
             EXPECT_EQ(summary.point_count(), points);
             EXPECT_EQ(summary.feature_count(), points);
             EXPECT_EQ(summary.distance(), 675412);
           });
  };

  *e();
}
