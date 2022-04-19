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

#include "test/grpc/route_guide/helper.h"
#include "test/grpc/route_guide/route_guide.eventuals.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"
#include "test/grpc/route_guide/route_guide_utilities.h"

using eventuals::Synchronizable;
using eventuals::grpc::ServerReader;
using routeguide::Feature;
using routeguide::Point;
using routeguide::RouteNote;
using routeguide::eventuals::RouteGuide;

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

<<<<<<< Updated upstream
  // auto RouteChat(grpc::ServerContext* context,
  // ServerReader<RouteNote>& reader) {
  // return reader.Read()
  //     | FlatMap(Let(
  //         [this, notes = std::vector<RouteNote>()]
  //(RouteNote& note) mutable {
  //           return Synchronized(Then([&]() {
  //                    for (const RouteNote& n : received_notes_) {
  //                      if (n.location().latitude()
  //                              == note.location().latitude()
  //                          && n.location().longitude()
  //                              == note.location().longitude()) {
  //                        notes.push_back(n);
  //                      }
  //                    }
  //                    received_notes_.push_back(note);
  //                  }))
  //               | Closure([&]() {
  //                    return Iterate(std::move(notes));
  //                  });
  //         }))
  //     | Map([](auto) {
  //          return eventuals::Catch()
  //              .raised<std::exception>([](std::exception&& e) {
  //                // return RequestType_();
  //                // return RouteSummary();
  //              });
  //        });
  // }
=======
  auto RouteChat(
      grpc::ServerContext* context,
      ServerReader<RouteNote>& reader) {
    return reader.Read()
        | FlatMap(Let(
            [this, notes = std::vector<RouteNote>()](RouteNote note) mutable {
              return Synchronized(
                         Then([&]() {
                           for (const RouteNote& n : received_notes_) {
                             if (n.location().latitude()
                                     == note.location().latitude()
                                 && n.location().longitude()
                                     == note.location().longitude()) {
                               notes.push_back(n);
                             }
                           }
                           received_notes_.push_back(note);
                         }))
                  | Closure([&]() {
                       CHECK(false);
                       return Iterate(std::move(notes));
                     });
            }));
  }
>>>>>>> Stashed changes

 private:
  std::vector<Feature> feature_list_;
  std::vector<RouteNote> received_notes_;
};

////////////////////////////////////////////////////////////////////////
