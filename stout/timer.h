#pragma once

#include "stout/event-loop.h"

namespace stout {
namespace eventuals {

inline auto Timer(const std::chrono::milliseconds& milliseconds) {
  return Clock().Timer(milliseconds);
}

} // namespace eventuals
} // namespace stout
