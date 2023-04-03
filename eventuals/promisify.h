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
[[nodiscard]] auto Promisify(
    std::string&& name,
    E e,
    EventLoop* loop = nullptr) {
  using Value = typename E::template ValueFrom<void, std::tuple<>>;

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
      >> std::move(e)
      >> Terminal()
             .context(std::move(promise))
             .start([loop](auto& promise, auto&&... values) {
               static_assert(
                   sizeof...(values) == 0 || sizeof...(values) == 1,
                   "'Promisify()' only supports 0 or 1 value, but found > 1");

               // NOTE: we need to _copy_ 'loop' before it possibly
               // gets freed due to a __different__ thread completing
               // the promise here via 'promise.set_value()' which may
               // cause the entire continuation 'k' to get freed.
               //
               // We use 'volatile' so that the compiler won't
               // optimize away this copy.
               volatile EventLoop* volatile_loop = loop;

               promise.set_value(std::forward<decltype(values)>(values)...);

               ////////////////////////////////////////////////////
               // NOTE: can't use any variables like 'loop' at   //
               // this point in time because they might have     //
               // been deallocated!                              //
               ////////////////////////////////////////////////////

               // Interrupt the event loop if provided in case it is
               // blocked on 'RunOnce()' in 'operator()*' below.
               if (volatile_loop != nullptr) {
                 const_cast<EventLoop*>(volatile_loop)->Interrupt();
               }
             })
             .fail([loop](auto& promise, auto&& error) {
               static_assert(
                   !std::is_same_v<
                       std::decay_t<decltype(error)>,
                       std::exception_ptr>,
                   "Not expecting a 'std::exception_ptr' to "
                   "propagate through an eventual");
               // NOTE: we need to _copy_ 'loop' before it possibly
               // gets freed due to a __different__ thread completing
               // the promise here via 'promise.set_value()' which may
               // cause the entire continuation 'k' to get freed.
               //
               // We use 'volatile' so that the compiler won't
               // optimize away this copy.
               volatile EventLoop* volatile_loop = loop;

               promise.set_exception(
                   std::make_exception_ptr(
                       std::forward<decltype(error)>(error)));

               ////////////////////////////////////////////////////
               // NOTE: can't use any variables like 'loop' at   //
               // this point in time because they might have     //
               // been deallocated!                              //
               ////////////////////////////////////////////////////

               // Interrupt the event loop if provided in case it is
               // blocked on 'RunOnce()' in 'operator()*' below.
               if (volatile_loop != nullptr) {
                 const_cast<EventLoop*>(volatile_loop)->Interrupt();
               }
             })
             .stop([loop](auto& promise) {
               // NOTE: we need to _copy_ 'loop' before it possibly
               // gets freed due to a __different__ thread completing
               // the promise here via 'promise.set_value()' which may
               // cause the entire continuation 'k' to get freed.
               //
               // We use 'volatile' so that the compiler won't
               // optimize away this copy.
               volatile EventLoop* volatile_loop = loop;

               promise.set_exception(
                   std::make_exception_ptr(
                       Stopped()));

               ////////////////////////////////////////////////////
               // NOTE: can't use any variables like 'loop' at   //
               // this point in time because they might have     //
               // been deallocated!                              //
               ////////////////////////////////////////////////////

               // Interrupt the event loop if provided in case it is
               // blocked on 'RunOnce()' in 'operator()*' below.
               if (volatile_loop != nullptr) {
                 const_cast<EventLoop*>(volatile_loop)->Interrupt();
               }
             }));

  return std::make_tuple(std::move(future), std::move(k));
}

////////////////////////////////////////////////////////////////////////

// Runs an eventual using the current thread.
//
// NOTE: THIS IS BLOCKING! CONSIDER YOURSELF WARNED!
template <typename E, std::enable_if_t<HasValueFrom<E>::value, int> = 0>
auto Run(E e) {
  try {
    auto [future, k] = Promisify(
        // NOTE: using the current thread id in order to constuct a task
        // name because the thread blocks so this name should be unique!
        "[thread "
            + stringify(std::this_thread::get_id())
            + " blocking on dereference]",
        std::move(e),
        EventLoop::HasDefault() ? &EventLoop::Default() : nullptr);

    k.Start();

    if (EventLoop::HasDefault()) {
      while (future.wait_for(std::chrono::seconds::zero())
             != std::future_status::ready) {
        EventLoop::Default().RunOnce();
      }
    }

    return future.get();
  } catch (const Error& e) {
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

// Overload of the dereference operator for eventuals which is
// "syntactic sugar" for calling 'Run()'.
//
// NOTE: THIS IS BLOCKING! CONSIDER YOURSELF WARNED!
template <typename E, std::enable_if_t<HasValueFrom<E>::value, int> = 0>
auto operator*(E e) {
  return Run(std::move(e));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
