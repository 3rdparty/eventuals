
#include <iostream>
#include <string>

#include "eventuals/do-all.h"
#include "eventuals/event-loop.h"
#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"

using grpc::Status;

using eventuals::DoAll;
using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;
using eventuals::grpc::Stream;

auto RouteGuideClient::RouteChat() {
  return client_.Call<
             RouteGuide,
             Stream<RouteNote>,
             Stream<RouteNote>>("RouteChat")
      | Then(Let([&](auto& call) {
           return DoAll(
                      Foreach(
                          Iterate(
                              {MakeRouteNote(data_[0]),
                               MakeRouteNote(data_[1]),
                               MakeRouteNote(data_[2]),
                               MakeRouteNote(data_[3])}),
                          [&](RouteNote note) {
                            return call.Writer().Write(note);
                          })
                          | call.WritesDone(),
                      Foreach(
                          call.Reader().Read(),
                          [this](RouteNote&& note) {
                            EXPECT_EQ(
                                note.message(),
                                std::get<0>(data_[current_]) + " bounced");
                            EXPECT_EQ(
                                note.location().latitude(),
                                std::get<1>(data_[current_]));
                            EXPECT_EQ(
                                note.location().longitude(),
                                std::get<2>(data_[current_++]));
                          }))
               | Finally([&](auto) {
                    return call.Finish();
                  })
               | Then([this](Status&& status) {
                    CHECK(status.ok()) << status.error_code()
                                       << ": " << status.error_message();
                    CHECK_EQ(current_, data_.size());

                    return true;
                  });
         }));
}

TEST_F(RouteGuideTest, RouteChatTest) {
  EXPECT_TRUE(*guide.RouteChat());
}
