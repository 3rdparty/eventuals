#pragma once

#include <deque>
#include <mutex>
#include <optional>

#include "eventuals/iterate.h"
#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Uses the eventual returned from calling the specified function 'f'
// to handle each value in the stream concurrently.
//
// Concurrent here means that every eventual returned from calling 'f'
// will have it's own scheduler context but it will use the default
// scheduler which is preemptive, i.e., no threads or other execution
// resources will be used. We call each eventual with it's own
// scheduling context a "fiber".
//
// The eventual returned from calling 'f' should be a generator, i.e.,
// it can compose with an "upstream" stream and itself is a stream
// (for example, it should be a 'Map()' or 'FlatMap()').
//
// If one of the eventuals raises a failure or stops, then we attempt
// to call "done" on the upstream stream, wait for all of the
// eventuals to finish, and then propagate the failure or stop
// downstream. There is one caveat to this case which is that we can't
// attempt to tell the upstream we're done until it has called down
// into us which means if we've called 'Next()' and it hasn't returned
// then we could wait forever. For now, the way to rectify this
// situation is to make sure that you interrupt the upstream stream
// you are composing with (in the future we'll be adding something
// like 'TypeErasedStream::Interrupt()' to support this case directly.
template <typename F>
[[nodiscard]] auto Concurrent(F f);

////////////////////////////////////////////////////////////////////////

struct _Concurrent final {

  struct Continuation {



    Ingress_ ingress_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable final {
    using E_ = typename std::invoke_result_t<F_>;

    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = typename E_::template ErrorsFrom<Arg, Errors>;

    template <typename Arg, typename K>
    auto k(K k) && {
      static_assert(
          !std::is_void_v<ValueFrom<Arg>>,
          "'Concurrent' does not (yet) support 'void' eventual values");

      auto egress = Build(
          Promise(),
          std::move(k));

      return Closure([f = std::move(f),
                      context = std::optional<Scheduler::Context>(),
                      egress = std::move(egress)]() {
                             context.emplace(
          Scheduler::Context::Get()->name() + " ]" + (*data)->name);

      (*data)->context->scheduler()->Submit(
          [&]() {
            CHECK_EQ(
                &(*data)->context.value(),
                Scheduler::Context::Get().get());
            k.Register((*data)->interrupt);
            k.Start();
          },
          (*data)->context.value());
                       
        return Ingress(std::move(f));
      });
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Concurrent(F f) {
  static_assert(
      std::is_invocable_v<F>,
      "Concurrent expects callable that takes no arguments");

  return _Concurrent::Composable<F>{std::move(f)};

  static std::atomic<int> i = 0;

  auto promise = Promise(
                     "[Concurrent - " + std::to_string(i++) + "]",
                     Iterate({std::move(value)})
                         >> f()
                         >> Finally([](auto value) {
                             return values.Write(std::move(value));
                           }))
      >> Finally([](auto future) {
                   CHECK(future) << "'Promise' should never fail or stop";
                   return futures.Write(std::move(future));
                 })
      >> Terminal();


  auto [future, promise] = Promise(
      "[Concurrent - " + std::to_string(i++) + "]",
      Iterate({std::move(value)})
          >> f());

  return futures.Write(std::move(future));


  // Upstream:
  return OnBegin([]() {
           return downstream_begin.Wait();
         })
      >> Until([this](auto&...) {
           return Synchronized([this]() {
             return downstream_done;
           });
         })
      >> Map([](auto... value) {
           auto [future, k] = 
               Promise(
                   "[Concurrent - " + std::to_string(i++) + "]",
                   Iterate({std::move(value)...})
                       >> f()
                       >> Finally([&](auto expected) {
                           return expecteds.Write(std::move(expected));
                                  }));

           auto& promise = promises.emplace_back(std::move(promise));

           

           >> Finally([](auto future) {
                   CHECK(future) << "'Promise' should never fail or stop";
                   return futures.Write(std::move(future));
                 })
               >> Terminal());

           promise.Start();
         })
      >> Loop()
      >> Finally([]() {
           return InterruptAllFutures()
               >> WaitForAllFutures();
         })
      >> upstream_done.Notify()
      >> Terminal();

  // Downstream:

  values.Read()
      >> OnBegin([]() {
          return upstream_begin.Notify();
        })
      >> Map([](auto value) {
          // Convert expected into eventual!
          return std::move(value);
        })
      >> OnEnded([&]() {
          // tell upstream we're done
          return downstream_done.Notify()
            >> upstream_done.Wait();
        });

  Select(

  >> Finally(Let([](auto&& future) {
    CHECK(future) << "'Promise' should never fail or stop";
    return future->Wait()
        >> Then([&]() {
             futures.Write(std::move(future.value()));
           });
  }))
      >> Terminal();


  | Finally([](auto&& future) {
                   CHECK(future) << "'Promise' should never fail or stop";
                   return futures.Write(std::forward<decltype(future)>(future));
                 })
      | Terminal();
  

  return Closure([lock = Lazy<Lock>(), fibers = std::unique_ptr<TypeErasedFiber>()]() {


                   
                   
    return Until([&](auto&... value) {
             return Acquire(*lock)
                 >> Then([&]() {
                      return CreateOrReuseFiber<decltype(value)>()
                          | Then([&](TypeErasedFiber* fiber) {
                               // A nullptr indicates that we should tell
                               // upstream we're "done" because something
                               // failed or an interrupt was received.
                               bool done = fiber == nullptr;

                               if (!done) {
                                 StartFiber(fiber, arg);
                               }

                               return done;
                             });
                    })
                 >> Release(*lock);
           })
      >> Buffer([&]() {
                   return futures.Read();
                 })
      >> 
      


}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
