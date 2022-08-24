#pragma once

#include <gtest/gtest.h>

#include <vector>

#include "eventuals/grpc/client.h"
#include "test/grpc/route_guide/route-guide-eventuals-server.h"
#include "test/grpc/route_guide/route_guide.eventuals.h"

////////////////////////////////////////////////////////////////////////

class RouteGuideTest : public ::testing::Test {
 public:
  RouteGuideTest();

  const std::vector<routeguide::Feature> feature_list;

  routeguide::eventuals::RouteGuide::Client CreateClient();

 protected:
  void SetUp() override;

 private:
  RouteGuideImpl service_;
  std::unique_ptr<eventuals::grpc::Server> server_;
  stout::Borrowable<eventuals::grpc::ClientCompletionThreadPool> pool_;
};

////////////////////////////////////////////////////////////////////////
