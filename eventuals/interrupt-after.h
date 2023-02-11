#pragma once

#include <optional>
#include <type_traits>
#include <variant>

#include "eventuals/closure.h"
#include "eventuals/compose.h"
#include "eventuals/finally.h"
#include "eventuals/let.h"
#include "eventuals/notification.h"
#include "eventuals/promise.h"
#include "eventuals/then.h"
#include "eventuals/timer.h"
#include "eventuals/type-traits.h"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E>
[[nodiscard]] auto InterruptAfter(std::chrono::nanoseconds nanoseconds, E e) {
  static_assert(HasValueFrom<E>::value, "'InterruptAfter' expects an eventual");

  return Closure([nanoseconds,
                  e = std::move(e),
                  notification = Lazy::Of<Notification>()]() mutable {
    // NOTE: not using 'DoAll' because that would create redundant
    // scheduler contexts.
    return Promise(
               "[interrupt-after-eventual]",
               [&]() {
                 return std::move(e)
                     >> Finally(Let([&](auto&& expected) {
                          return notification->Notify()
                              >> Then([&]() {
                                   return std::move(expected);
                                 });
                        }));
               })
        // TODO(benh): use 'Finally'.
        | Then(Let([&](auto& e_future) {
             // TODO(check if the future is already completed in
             // which case no need to create timer!
             return Promise(
                        "[interrupt-after-timer]",
                        [&]() {
                          return Timer(nanoseconds)
                              >> Finally([&](auto&&) {
                                   return notification->Notify();
                                 });
                        })
                   // TODO(benh): use 'Finally'.
                 >> Then(Let([&](auto& timer_future) {
                      // TODO(benh): remove this once 'Notification'
                      // (or actually, 'ConditionVariable') supports
                      // interrupts and then use 'Finally' instead of
                      // 'Then' below.
                      return Eventual<void>()
                                 .interruptible()
                                 .context([&]() {
                                   timer_future.Interrupt();
                                   e_future.Interrupt();
                                 })
                                 .start([&](auto& callback,
                                            auto& k,
                                            auto& handler) {
                                   if (handler) {
                                     bool installed = handler->Install([&]() {
                                       callback();
                                     });

                                     if (!installed) {
                                       // An interrupt has been triggered!
                                       callback();
                                     }
                                   }

                                   k.Start();
                                 })
                          >> notification->Wait()
                          >> Then([&]() {
                               timer_future.Interrupt();
                               e_future.Interrupt();
                               return timer_future.Wait()
                                   >> std::move(e_future).Get();
                             });
                    }));
           }));
  });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
