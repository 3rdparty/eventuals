
#include <iostream>
#include <string>

#include "eventuals/do-all.h"
#include "eventuals/event-loop.h"
#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "helper.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"
#include "test/grpc/route_guide/route-guide-utilities.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::CompletionPool;
using eventuals::grpc::ServerBuilder;
using grpc::Status;

using stout::Borrowable;

using eventuals::DoAll;
using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Then;
using eventuals::grpc::Stream;
using routeguide::RouteNote;
using routeguide::eventuals::RouteGuide;

class RouteGuideRouteChatClient : public RouteGuideBaseClient {
 public:
  RouteGuideRouteChatClient(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : RouteGuideBaseClient(target, credentials, std::move(pool), db) {}

  auto RouteChat() {
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

 private:
  size_t current_ = 0;
  const std::vector<std::tuple<std::string, long, long>> data_ = {
      {"First message", 0, 0},
      {"Second message", 0, 1},
      {"Third message", 1, 0},
      {"Fourth message", 0, 0}};
};

TEST(RouteGuideTest, RouteChatTest) {
  std::string db_path("test/grpc/route_guide/route_guide_db.json");
  std::string db;
  routeguide::GetDbFileContent(db_path, db);

  RouteGuideImpl service(db);

  ServerBuilder builder;
  std::string server_address("localhost:45554");

  builder.AddListeningPort(
      server_address,
      grpc::InsecureServerCredentials());

  builder.RegisterService(&service);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  Borrowable<CompletionPool> pool;

  RouteGuideRouteChatClient guide(
      server_address,
      grpc::InsecureChannelCredentials(),
      pool.Borrow(),
      db);

  EXPECT_TRUE(*guide.RouteChat());
}
