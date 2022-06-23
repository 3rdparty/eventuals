#pragma once

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "eventuals/do-all.h"
#include "eventuals/foreach.h"
#include "eventuals/grpc/client.h"
#include "eventuals/range.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"
#include "test/grpc/route_guide/route_guide_generated/route_guide.client.eventuals.h"

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using routeguide::Feature;
using routeguide::Point;
using routeguide::Rectangle;
using stout::Borrowable;

using eventuals::grpc::Stream;
using grpc::Status;

using eventuals::DoAll;
using eventuals::Finally;
using eventuals::Foreach;
using eventuals::Head;
using eventuals::Let;
using eventuals::Range;

////////////////////////////////////////////////////////////////////////

class RouteGuideClientImpl : public RouteGuideClient {
 public:
  RouteGuideClientImpl(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : RouteGuideClient(target, credentials, std::move(pool)) {}

  void SetDb(const std::string& db);

  auto GetFeatureTest();

  int GetPointsCount() {
    return kPoints_;
  }

  std::vector<Feature> feature_list_;

  const size_t left_ = -750000000;
  const size_t right_ = -730000000;
  const size_t top_ = 420000000;
  const size_t bottom_ = 400000000;
  size_t current_ = 0;
  std::vector<Feature> filtered_;
  const float kCoordFactor_ = 10000000.0;
  const int kPoints_ = 10;

  const std::vector<std::tuple<std::string, long, long>> data_ = {
      {"First message", 0, 0},
      {"Second message", 0, 1},
      {"Third message", 1, 0},
      {"Fourth message", 0, 0}};
};

////////////////////////////////////////////////////////////////////////

class RouteGuideTest : public ::testing::Test {
 public:
  RouteGuideTest();

 protected:
  void SetUp() override;

 private:
  const std::string db_path_ = "test/grpc/route_guide/route_guide_db.json";
  const std::string server_address_ = "localhost:8888";

  RouteGuideImpl service_;
  std::unique_ptr<eventuals::grpc::Server> server_;
  Borrowable<CompletionPool> pool_;

 public:
  //  Need to be declared after 'pool_'.
  RouteGuideClientImpl guide;
};

////////////////////////////////////////////////////////////////////////
