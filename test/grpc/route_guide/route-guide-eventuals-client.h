#pragma once

#include <grpcpp/channel.h>

#include <memory>
#include <string>
#include <vector>

#include "eventuals/grpc/client.h"
#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::ServerBuilder;
using eventuals::grpc::ServerStatusOrServer;
using routeguide::Feature;
using routeguide::Point;
using stout::Borrowable;

////////////////////////////////////////////////////////////////////////

class RouteGuideClient {
 public:
  RouteGuideClient(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : client_(target, credentials, std::move(pool)) {
  }

  void SetDb(const std::string& db) {
    routeguide::ParseDb(db, &feature_list_);
  }

  auto GetFeature();

  auto ListFeatures();

  auto RecordRoute();

  auto RouteChat();

 private:
  auto GetOneFeature(Point&& point);

  Client client_;

  std::vector<Feature> feature_list_;
  std::vector<Feature> filtered_;
  size_t current_ = 0;

  const float kCoordFactor_ = 10000000.0;
  const int kPoints_ = 10;

  const size_t left_ = -750000000;
  const size_t right_ = -730000000;
  const size_t top_ = 420000000;
  const size_t bottom_ = 400000000;

  const std::vector<std::tuple<std::string, long, long>> data_ = {
      {"First message", 0, 0},
      {"Second message", 0, 1},
      {"Third message", 1, 0},
      {"Fourth message", 0, 0}};
};

////////////////////////////////////////////////////////////////////////

class RouteGuideTest : public ::testing::Test {
 public:
  const std::string db_path_ = "test/grpc/route_guide/route_guide_db.json";
  const std::string server_address_ = "localhost:8888";

  RouteGuideImpl service_;
  std::unique_ptr<eventuals::grpc::Server> server_;
  Borrowable<CompletionPool> pool_;

 protected:
  void SetUp() override {
    std::string db;
    routeguide::GetDbFileContent(db_path_, db);

    service_.ParseDb(db);

    ServerBuilder builder;
    builder.AddListeningPort(
        server_address_,
        ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);

    auto build = builder.BuildAndStart();

    ASSERT_TRUE(build.status.ok()) << build.status;
    server_.swap(build.server);
    ASSERT_TRUE(server_);

    guide.SetDb(db);
  }

 public:
  RouteGuideTest()
    : guide(
        server_address_,
        ::grpc::InsecureChannelCredentials(),
        pool_.Borrow(),
        "") {}

  RouteGuideClient guide;
};

////////////////////////////////////////////////////////////////////////
