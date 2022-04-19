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

#include <string>
#include <vector>

#include "eventuals/closure.h"
#include "eventuals/filter.h"
#include "eventuals/flat-map.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/then.h"
#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route-guide-utilities.h"
#include "test/grpc/route_guide/route_guide.eventuals.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using eventuals::Closure;
using eventuals::Filter;
using eventuals::FlatMap;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Synchronizable;
using eventuals::Then;
using eventuals::grpc::ServerReader;
using routeguide::Feature;
using routeguide::Point;
using routeguide::Rectangle;
using routeguide::RouteNote;
using routeguide::RouteSummary;
using routeguide::eventuals::RouteGuide;
using std::chrono::system_clock;

////////////////////////////////////////////////////////////////////////

class RouteGuideImpl final
  : public RouteGuide::Service<RouteGuideImpl>,
    public Synchronizable {
 public:
  explicit RouteGuideImpl(const std::string& db) {
    routeguide::ParseDb(db, &feature_list_);
  }

  auto GetFeature(grpc::ServerContext* context, Point&& point) {
    Feature feature;
    feature.set_name(GetFeatureName(point, feature_list_));
    feature.mutable_location()->CopyFrom(point);
    return feature;
  }

  auto ListFeatures(
      grpc::ServerContext* context,
      routeguide::Rectangle&& rectangle) {
    auto lo = rectangle.lo();
    auto hi = rectangle.hi();
    long left = std::min(lo.longitude(), hi.longitude());
    long right = std::max(lo.longitude(), hi.longitude());
    long top = std::max(lo.latitude(), hi.latitude());
    long bottom = std::min(lo.latitude(), hi.latitude());

    return Iterate(feature_list_)
        | Filter([left, right, top, bottom](const Feature& f) {
             return f.location().longitude() >= left
                 && f.location().longitude() <= right
                 && f.location().latitude() >= bottom
                 && f.location().latitude() <= top;
           });
  }

  auto RecordRoute(
      grpc::ServerContext* context,
      ServerReader<Point>& reader) {
    return Closure([this,
                    &reader,
                    point_count = 0,
                    feature_count = 0,
                    distance = 0.0,
                    previous = Point()]() mutable {
      return reader.Read()
          | Map([&](Point&& point) {
               point_count++;
               if (!GetFeatureName(point, feature_list_).empty()) {
                 feature_count++;
               }
               if (point_count != 1) {
                 distance += GetDistance(previous, point);
               }
               previous = point;
             })
          | Loop()
          | Then([&]() {
               RouteSummary summary;
               summary.set_point_count(point_count);
               summary.set_feature_count(feature_count);
               summary.set_distance(static_cast<long>(distance));
               return summary;
             });
    });
  }

  //  route-chat -> (notes :: ...RouteNote) -> ...RouteNote =
  //   ... foreach: notes {
  //     note ->
  //       server is:
  //         Idle { received-notes } ->
  //           set: server is: Chatting
  //           ... foreach: received-notes {
  //             n ->
  //               if: (n `location `latitude) == (note `location `latitude)
  //                   &&
  //(n `location `longitude) == (note `location `longitude)
  //               then: ...n
  //               else: ...]
  //             ...] ->
  //               set: received-notes =: `enqueue note
  //               ...]
  //           }
  //         _ -> skip: Idle
  //     ...] -> ...]
  //   }

  auto RouteChat(
      grpc::ServerContext* context,
      ServerReader<RouteNote>& reader) {
    return reader.Read()
        | FlatMap(Let(
            [this, notes = std::vector<RouteNote>()](RouteNote& note) mutable {
              return Synchronized(
                         Then([&]() {
                           RouteNote response = MakeRouteNote(
                               note.message() + " bounced",
                               note.location().latitude(),
                               note.location().longitude());

                           notes.push_back(response);
                         }))
                  | Closure([&]() {
                       return Iterate(std::move(notes));
                     });
            }));
  }

 private:
  std::vector<Feature> feature_list_;
};

////////////////////////////////////////////////////////////////////////
