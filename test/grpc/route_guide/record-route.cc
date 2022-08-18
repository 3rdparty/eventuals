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
#include "test/grpc/route_guide/route-guide-eventuals-test.h"

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
  std::vector<Point> data(GetPointsCount());

  for (size_t i = 0; i < GetPointsCount(); ++i) {
    data[i] = feature_list_[i].location();
  }

  auto e = [&]() {
    return client->RecordRoute()
        >> Then(Let([&data](auto& call) mutable {
             return Foreach(
                        Iterate(std::move(data)),
                        ([&call](auto&& input) {
                          return call.Writer().Write(input);
                        }))
                 >> call.WritesDone()
                 >> call.Reader().Read()
                 >> Head()
                 >> Finally(Let([&call](auto& output) {
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
        >> Then([this](auto&& summary) {
             CHECK_EQ(summary.point_count(), GetPointsCount());
             CHECK_EQ(summary.feature_count(), GetPointsCount());
             CHECK_EQ(summary.distance(), 675412);
           });
  };

  *e();
}
