
#include <iostream>
#include <string>

#include "eventuals/do-all.h"
#include "eventuals/event-loop.h"
#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using grpc::Status;

using eventuals::DoAll;
using eventuals::Finally;
using eventuals::operator*;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;
using eventuals::grpc::Stream;

TEST_F(RouteGuideTest, RouteChatTest) {
  auto e = [&]() {
    return guide.RouteChat()
        >> Then(Let([&](auto& call) {
             return DoAll(
                        Foreach(
                            Iterate(
                                {MakeRouteNote(guide.data_[0]),
                                 MakeRouteNote(guide.data_[1]),
                                 MakeRouteNote(guide.data_[2]),
                                 MakeRouteNote(guide.data_[3])}),
                            [&](RouteNote input) {
                              return call.Writer().Write(input);
                            })
                            >> call.WritesDone(),
                        Foreach(
                            call.Reader().Read(),
                            [&](auto&& note) {
                              EXPECT_EQ(
                                  note.message(),
                                  std::get<0>(
                                      guide.data_[guide.current_])
                                      + " bounced");
                              EXPECT_EQ(
                                  note.location().latitude(),
                                  std::get<1>(guide.data_[guide.current_]));
                              EXPECT_EQ(
                                  note.location().longitude(),
                                  std::get<2>(guide.data_[guide.current_++]));
                            }))
                 >> Finally([&](auto) {
                      return call.Finish();
                    })
                 >> Then([](Status&& status) {
                      CHECK(status.ok()) << status.error_code()
                                         << ": " << status.error_message();
                    });
           }))
        >> Then([&]() {
             CHECK_EQ(guide.current_, guide.data_.size());
             return true;
           });
  };

  EXPECT_TRUE(*e());
}
