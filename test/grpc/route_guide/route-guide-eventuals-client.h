/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

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
