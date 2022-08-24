#include <string>
#include <tuple>

#include "test/grpc/route_guide/route_guide.grpc.pb.h"

////////////////////////////////////////////////////////////////////////

inline routeguide::Point MakePoint(long latitude, long longitude) {
  routeguide::Point p;
  p.set_latitude(latitude);
  p.set_longitude(longitude);
  return p;
}

////////////////////////////////////////////////////////////////////////

inline routeguide::Feature MakeFeature(
    const std::string& name,
    long latitude,
    long longitude) {
  routeguide::Feature f;
  f.set_name(name);
  f.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
  return f;
}

////////////////////////////////////////////////////////////////////////

inline routeguide::RouteNote MakeRouteNote(
    const std::string& message,
    long latitude,
    long longitude) {
  routeguide::RouteNote n;
  n.set_message(message);
  n.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
  return n;
}

////////////////////////////////////////////////////////////////////////
