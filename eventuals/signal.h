#pragma once

#include "eventuals/concurrent.h"
#include "eventuals/event-loop.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/map.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto WaitForSignal(EventLoop& loop, int signum) {
  return loop.WaitForSignal(signum);
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto WaitForSignal(int signum) {
  return EventLoop::Default().WaitForSignal(signum);
}

////////////////////////////////////////////////////////////////////////

// Eventual that waits for one of the specified signals to be raised
// and then propagates the raised signal number to the next eventual.
//
// Note that all standard signal handling constraints still apply,
// i.e., you can't have more than one handler for the same signal,
// which in this case means you can't have more than one of
// outstanding calls to this function with the same signal.
//
// NOTE: we take an array instead of a 'std::initializer_list' because
// then we can 'std::move()' into 'Iterate()' without a copy.
template <size_t N>
[[nodiscard]] auto WaitForOneOfSignals(int(&&signums)[N]) {
  return Iterate(std::move(signums))
      | Concurrent([]() {
           return Map([](int signum) {
             return WaitForSignal(signum)
                 | Just(signum);
           });
         })
      | Head();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
