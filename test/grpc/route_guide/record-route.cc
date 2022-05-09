
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
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
using eventuals::Let;
using eventuals::Range;
using eventuals::Then;

auto RouteGuideClient::RecordRoute() {
  return client_.Call<RouteGuide, Stream<Point>, RouteSummary>("RecordRoute")
      | Then(Let(
          [this](auto& call) mutable {
            return Foreach(
                       Range(kPoints_),
                       ([&](int pos) {
                         const Feature& f = feature_list_[pos];
                         return call.Writer().Write(f.location());
                       }))
                | call.WritesDone()
                | call.Reader().Read()
                | Head()
                | Finally(Let([&](auto& stats) {
                     return call.Finish()
                         | Then([&](Status&& status) {
                              CHECK(status.ok()) << status.error_code()
                                                 << ": "
                                                 << status.error_message();
                              CHECK(stats);

                              CHECK_EQ(stats->point_count(), kPoints_);
                              CHECK_EQ(stats->feature_count(), kPoints_);
                              CHECK_EQ(stats->distance(), 675412);

                              return true;
                            });
                   }));
          }));
}

TEST_F(RouteGuideTest, RecordRouteTest) {
  EXPECT_TRUE(*guide.RecordRoute());
}
