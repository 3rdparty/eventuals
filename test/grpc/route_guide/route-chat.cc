#include <string>
#include <vector>

#include "eventuals/do-all.h"
#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-test.h"

using eventuals::DoAll;
using eventuals::Finally;
using eventuals::Iterate;
using eventuals::operator*;
using eventuals::Foreach;
using eventuals::Let;
using eventuals::Then;

using routeguide::RouteNote;

TEST_F(RouteGuideTest, RouteChatTest) {
  const std::vector<RouteNote> notes = {
      MakeRouteNote("First message", 0, 0),
      MakeRouteNote("Second message", 0, 1),
      MakeRouteNote("Third message", 1, 0),
      MakeRouteNote("Fourth message", 0, 0)};
  size_t current = 0;

  auto client = CreateClient();

  auto e = [&]() {
    return client.RouteChat()
        >> Then(Let([&](auto& call) {
             return DoAll(
                        Foreach(
                            Iterate(notes),
                            [&call](const RouteNote& note) {
                              return call.Writer().Write(note);
                            })
                            >> call.WritesDone(),
                        Foreach(
                            call.Reader().Read(),
                            [&](auto&& note) {
                              EXPECT_EQ(
                                  note.message(),
                                  notes[current].message() + " received");
                              EXPECT_EQ(
                                  note.location().latitude(),
                                  notes[current].location().latitude());
                              EXPECT_EQ(
                                  note.location().longitude(),
                                  notes[current++].location().longitude());
                            }))
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
             EXPECT_EQ(current, notes.size());
           });
  };

  *e();
}
