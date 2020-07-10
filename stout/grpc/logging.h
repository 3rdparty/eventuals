#pragma once

#include "glog/logging.h"

// TODO(benh): Initialize logging if it hasn't already been done so?

#define STOUT_GRPC_LOG                                                  \
  ({                                                                    \
    const char* value = std::getenv("STOUT_GRPC_LOG");                  \
    value != nullptr ? strcmp(value, "1") == 0 : false;                 \
  })

