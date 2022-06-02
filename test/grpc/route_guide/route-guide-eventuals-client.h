#pragma once

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "eventuals/grpc/client.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
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
      const std::string& db);

  void SetDb(const std::string& db);

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
  RouteGuideClient guide;
};

////////////////////////////////////////////////////////////////////////
