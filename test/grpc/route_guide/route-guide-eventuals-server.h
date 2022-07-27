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
#include "test/grpc/route_guide/route_guide.grpc.pb.h"
#include "test/grpc/route_guide/route_guide_generated/route_guide.eventuals.h"

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

inline Point MakePoint(long latitude, long longitude) {
  Point p;
  p.set_latitude(latitude);
  p.set_longitude(longitude);
  return p;
}

////////////////////////////////////////////////////////////////////////

inline Feature MakeFeature(
    const std::string& name,
    long latitude,
    long longitude) {
  Feature f;
  f.set_name(name);
  f.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
  return f;
}

////////////////////////////////////////////////////////////////////////

inline RouteNote MakeRouteNote(
    const std::string& message,
    long latitude,
    long longitude) {
  RouteNote n;
  n.set_message(message);
  n.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
  return n;
}

inline RouteNote MakeRouteNote(
    const std::tuple<std::string, long, long>& note) {
  RouteNote n;
  n.set_message(std::get<0>(note));
  n.mutable_location()->CopyFrom(
      MakePoint(std::get<1>(note), std::get<2>(note)));
  return n;
}

////////////////////////////////////////////////////////////////////////

inline float ConvertToRadians(float num) {
  return num * 3.1415926 / 180;
}

////////////////////////////////////////////////////////////////////////

// The formula is based on http://mathforum.org/library/drmath/view/51879.html
inline float GetDistance(const Point& start, const Point& end) {
  const float kCoordFactor = 10000000.0;
  float lat_1 = start.latitude() / kCoordFactor;
  float lat_2 = end.latitude() / kCoordFactor;
  float lon_1 = start.longitude() / kCoordFactor;
  float lon_2 = end.longitude() / kCoordFactor;
  float lat_rad_1 = ConvertToRadians(lat_1);
  float lat_rad_2 = ConvertToRadians(lat_2);
  float delta_lat_rad = ConvertToRadians(lat_2 - lat_1);
  float delta_lon_rad = ConvertToRadians(lon_2 - lon_1);

  float a = pow(
                sin(delta_lat_rad / 2),
                2)
      + cos(lat_rad_1) * cos(lat_rad_2) * pow(sin(delta_lon_rad / 2), 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  int R = 6371000; // metres

  return R * c;
}

////////////////////////////////////////////////////////////////////////

inline std::string GetFeatureName(
    const Point& point,
    const std::vector<Feature>& feature_list) {
  for (const Feature& f : feature_list) {
    if (f.location().latitude() == point.latitude()
        && f.location().longitude() == point.longitude()) {
      return f.name();
    }
  }
  return "";
}

////////////////////////////////////////////////////////////////////////

class RouteGuideImpl final
  : public RouteGuide::Service<RouteGuideImpl>,
    public Synchronizable {
 public:
  void ParseDb(const std::string& db);

  Feature GetFeature(grpc::ServerContext* context, Point&& point);

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
