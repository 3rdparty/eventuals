#pragma once

#include "eventuals/closure.h"
#include "eventuals/compose.h"
#include "eventuals/event-loop.h"
#include "eventuals/lazy.h"
#include "eventuals/scheduler.h"
#include "eventuals/terminal.h"
#include "stout/borrowable.h"
#include "stout/stringify.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper that "promisifies" an eventual, i.e., builds and returns a
// continuation 'k' that you can start along with a 'std::future' that
// you can use to wait for the eventual value.
//
// NOTE: uses the default, i.e., preemptive, scheduler so that the
// eventual has it's own 'Scheduler::Context'.
template <typename E>
[[nodiscard]] auto Promisify(std::string&& name, E e) {
  using Value = typename E::template ValueFrom<void>;

  std::promise<
      typename ReferenceWrapperTypeExtractor<Value>::type>
      promise;

  auto future = promise.get_future();

  auto k = Build(
      Closure([context = Lazy<Scheduler::Context>(
                   Scheduler::Default(),
                   std::move(name))]() mutable {
        // NOTE: intentionally rescheduling with our context and never
        // rescheduling again because when we terminate we're done!
        return Reschedule(context->Borrow());
      })
      | std::move(e)
      | Terminal()
            .context(std::move(promise))
            .start([](auto& promise, auto&&... values) {
              static_assert(
                  sizeof...(values) == 0 || sizeof...(values) == 1,
                  "'Promisify()' only supports 0 or 1 value, but found > 1");
              promise.set_value(std::forward<decltype(values)>(values)...);
            })
            .fail([](auto& promise, auto&& error) {
              promise.set_exception(
                  make_exception_ptr_or_forward(
                      std::forward<decltype(error)>(error)));
            })
            .stop([](auto& promise) {
              promise.set_exception(
                  std::make_exception_ptr(
                      StoppedException()));
            }));

  return std::make_tuple(std::move(future), std::move(k));
}

////////////////////////////////////////////////////////////////////////

// Overload of the dereference operator for eventuals.
//
// NOTE: THIS IS BLOCKING! CONSIDER YOURSELF WARNED!
template <typename E, std::enable_if_t<HasValueFrom<E>::value, int> = 0>
auto operator*(E e) {
  try {
    auto [future, k] = Promisify(
        // NOTE: using the current thread id in order to constuct a task
        // name because the thread blocks so this name should be unique!
        "[thread "
            + stringify(std::this_thread::get_id())
            + " blocking on dereference]",
        std::move(e));

    k.Start();

    if (EventLoop::HasDefault()) {
      EventLoop::Default().RunUntil(future);
    }

    return future.get();
  } catch (const std::exception& e) {
    LOG(WARNING)
        << "WARNING: exception thrown while dereferencing eventual: "
        << e.what();
    throw;
  } catch (...) {
    LOG(WARNING)
        << "WARNING: exception thrown while dereferencing eventual";
    throw;
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
