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
#include "test/grpc/route_guide/make.h"
#include "test/grpc/route_guide/route_guide.eventuals.h"
#include "test/grpc/route_guide/route_guide.grpc.pb.h"

////////////////////////////////////////////////////////////////////////

inline float ConvertToRadians(float num) {
  return num * 3.1415926 / 180;
}

////////////////////////////////////////////////////////////////////////

// The formula is based on http://mathforum.org/library/drmath/view/51879.html
inline float GetDistance(
    const routeguide::Point& start,
    const routeguide::Point& end) {
  const float kCoordFactor = 10000000.0;
  float lat_1 = start.latitude() / kCoordFactor;
  float lat_2 = end.latitude() / kCoordFactor;
  float lon_1 = start.longitude() / kCoordFactor;
  float lon_2 = end.longitude() / kCoordFactor;
  float lat_rad_1 = ConvertToRadians(lat_1);
  float lat_rad_2 = ConvertToRadians(lat_2);
  float delta_lat_rad = ConvertToRadians(lat_2 - lat_1);
  float delta_lon_rad = ConvertToRadians(lon_2 - lon_1);

  float a = pow(sin(delta_lat_rad / 2), 2)
      + cos(lat_rad_1) * cos(lat_rad_2) * pow(sin(delta_lon_rad / 2), 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  int R = 6371000; // metres

  return R * c;
}

////////////////////////////////////////////////////////////////////////

inline std::string GetFeatureName(
    const routeguide::Point& point,
    const std::vector<routeguide::Feature>& feature_list) {
  for (const routeguide::Feature& f : feature_list) {
    if (f.location().latitude() == point.latitude()
        && f.location().longitude() == point.longitude()) {
      return f.name();
    }
  }
  return "";
}

////////////////////////////////////////////////////////////////////////

class RouteGuideImpl final
  : public routeguide::eventuals::RouteGuide::Service<RouteGuideImpl>,
    public eventuals::Synchronizable {
 public:
  RouteGuideImpl(const std::vector<routeguide::Feature>& feature_list)
    : feature_list_(feature_list) {}

  routeguide::Feature GetFeature(
      grpc::ServerContext* context,
      routeguide::Point&& point) {
    routeguide::Feature feature;
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

    return eventuals::Iterate(feature_list_)
        >> eventuals::Filter(
               [left, right, top, bottom](const routeguide::Feature& f) {
                 return f.location().longitude() >= left
                     && f.location().longitude() <= right
                     && f.location().latitude() >= bottom
                     && f.location().latitude() <= top;
               });
  }

  auto RecordRoute(
      grpc::ServerContext* context,
      eventuals::grpc::ServerReader<routeguide::Point>& reader) {
    return eventuals::Closure([this,
                               &reader,
                               point_count = 0,
                               feature_count = 0,
                               distance = 0.0,
                               previous = routeguide::Point()]() mutable {
      return reader.Read()
          >> eventuals::Map([&](routeguide::Point&& point) {
               point_count++;
               if (!GetFeatureName(point, feature_list_).empty()) {
                 feature_count++;
               }
               if (point_count != 1) {
                 distance += GetDistance(previous, point);
               }
               previous = point;
             })
          >> eventuals::Loop()
          >> eventuals::Then([&]() {
               routeguide::RouteSummary summary;
               summary.set_point_count(point_count);
               summary.set_feature_count(feature_count);
               summary.set_distance(static_cast<long>(distance));
               return summary;
             });
    });
  }

  auto RouteChat(
      grpc::ServerContext* context,
      eventuals::grpc::ServerReader<routeguide::RouteNote>& reader) {
    return reader.Read()
        >> eventuals::FlatMap(eventuals::Let(
            [this, notes = std::vector<routeguide::RouteNote>()](
                routeguide::RouteNote& note) mutable {
              return Synchronized(
                         eventuals::Then([&]() {
                           routeguide::RouteNote response = MakeRouteNote(
                               note.message() + " received",
                               note.location().latitude(),
                               note.location().longitude());

                           notes.push_back(response);
                         }))
                  >> eventuals::Closure([&]() {
                       return eventuals::Iterate(std::move(notes));
                     });
            }));
  }

 private:
  const std::vector<routeguide::Feature> feature_list_;
};

////////////////////////////////////////////////////////////////////////
