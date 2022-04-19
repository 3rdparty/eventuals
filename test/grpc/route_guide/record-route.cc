
#include <string>

#include "eventuals/finally.h"
#include "eventuals/foreach.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/range.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "helper.h"
#include "test/grpc/route_guide/route-guide-eventuals-client.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"
#include "test/grpc/route_guide/route-guide-utilities.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::CompletionPool;
using eventuals::grpc::ServerBuilder;
using eventuals::grpc::Stream;
using grpc::Status;

using stout::Borrowable;

using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Range;
using eventuals::Then;

class RouteGuideRecordRouteClient : public RouteGuideBaseClient {
 public:
  RouteGuideRecordRouteClient(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : RouteGuideBaseClient(target, credentials, std::move(pool), db) {}

  auto RecordRoute() {
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

 private:
  const int kPoints_ = 10;
};

TEST(RouteGuideTest, RecordRouteTest) {
  std::string db_path("test/grpc/route_guide/route_guide_db.json");

  std::string db;
  routeguide::GetDbFileContent(db_path, db);

  RouteGuideImpl service(db);

  ServerBuilder builder;
  std::string server_address("localhost:8888");

  builder.AddListeningPort(
      server_address,
      grpc::InsecureServerCredentials());

  builder.RegisterService(&service);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  Borrowable<CompletionPool> pool;

  RouteGuideRecordRouteClient guide(
      server_address,
      grpc::InsecureChannelCredentials(),
      pool.Borrow(),
      db);

  EXPECT_TRUE(*guide.RecordRoute());
}
