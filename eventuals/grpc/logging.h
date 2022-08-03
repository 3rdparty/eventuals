#pragma once

#include "glog/logging.h"

inline bool EventualsGrpcLog(int level) {
  // TODO(benh): Initialize logging if it hasn't already been done so?
  static const char* variable = std::getenv("EVENTUALS_GRPC_LOG");
  static int value = variable != nullptr ? atoi(variable) : 0;
  return value >= level;
}

#define EVENTUALS_GRPC_LOG(level) LOG_IF(INFO, EventualsGrpcLog(level))
