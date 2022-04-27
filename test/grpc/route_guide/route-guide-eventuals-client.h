#pragma once

#include <grpcpp/channel.h>

#include <memory>
#include <string>
#include <vector>

#include "eventuals/grpc/client.h"
#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using routeguide::Feature;
using routeguide::Point;
using stout::Borrowable;

////////////////////////////////////////////////////////////////////////

class RouteGuideBaseClient {
 public:
  RouteGuideBaseClient(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool,
      const std::string& db)
    : client_(target, credentials, std::move(pool)) {
    routeguide::ParseDb(db, &feature_list_);
  }

 protected:
  Client client_;
  std::vector<Feature> feature_list_;
};

////////////////////////////////////////////////////////////////////////
