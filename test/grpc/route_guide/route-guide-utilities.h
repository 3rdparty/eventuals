#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "test/grpc/route_guide/route_guide.grpc.pb.h"

using routeguide::Feature;
using routeguide::Point;
using routeguide::RouteNote;

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
