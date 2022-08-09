
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/range.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using eventuals::grpc::Stream;
using grpc::Status;

using eventuals::Finally;
using eventuals::Foreach;
using eventuals::operator*;
using eventuals::Head;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Range;
using eventuals::Then;

using routeguide::Point;

TEST_F(RouteGuideTest, RecordRouteTest) {
  std::vector<Point> data(guide.GetPointsCount());

  for (size_t i = 0; i < guide.GetPointsCount(); ++i) {
    data[i] = guide.feature_list_[i].location();
  }

  auto e = [&]() {
    return guide.RecordRoute()
        >> Then(Let([&](auto& call) mutable {
             return Foreach(
                        Iterate(std::move(data)),
                        ([&](auto&& input) {
                          return call.Writer().Write(input);
                        }))
                 >> call.WritesDone()
                 >> call.Reader().Read()
                 >> Head()
                 >> Finally(Let([&](auto& output) {
                      return call.Finish()
                          >> Then([&](Status&& status) {
                               CHECK(status.ok()) << status.error_code()
                                                  << ": "
                                                  << status.error_message();
                               CHECK(output);
                               return std::move(output);
                             });
                    }));
           }))
        >> Then([&](auto&& summary) {
             CHECK_EQ(summary.point_count(), guide.GetPointsCount());
             CHECK_EQ(summary.feature_count(), guide.GetPointsCount());
             CHECK_EQ(summary.distance(), 675412);

             return true;
           });
  };

  EXPECT_TRUE(*e());
}
