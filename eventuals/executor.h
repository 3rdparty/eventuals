#pragma once

#include "eventuals/catch.h"
#include "eventuals/concurrent.h"
#include "eventuals/control-loop.h"
#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/pipe.h"
#include "eventuals/type-check.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for running "fire and forget" eventuals. The eventual you
// submit _should not_ be terminated, but you can still determine when
// your eventuals have completed by composing with a 'Finally()' or a
// raw 'Eventual()` that handles each possibility.
template <typename E_>
class Executor final : public Synchronizable {
 public:
  static_assert(
      HasValueFrom<E_>::value,
      "'Executor' expects an eventual type to execute");

  Executor(std::string&& name)
    : name_(std::move(name)),
      control_loop_(
          std::string(name_),
          [this]() {
            // TODO(benh): use 'StaticThreadPool' or some other
            // scheduler to ensure that execution will happen
            // asynchronously instead of preemptively!
            return pipe_.Read()
                | Concurrent([this]() {
                     return Map([this](E_ e) {
                       // NOTE: we do 'RescheduleAfter()' here so that
                       // we make sure we don't end up borrowing any
                       // 'Scheduler::Context' used within 'e' which
                       // may cause a deadlock if 'this' gets
                       // destructed after the borrowed
                       // 'Scheduler::Context'.
                       return RescheduleAfter(std::move(e))
                           // NOTE: returning 'std::monostate' since
                           // 'Concurrent' does not yet support 'void'.
                           | Just(std::monostate{})
                           | Catch()
                                 .raised<std::exception>(
                                     [this](std::exception&& e) {
                                       EVENTUALS_LOG(1)
                                           << "Executor '" << name_
                                           << "' caught: " << e.what();
                                       return std::monostate{};
                                     });
                     });
                   })
                | Loop();
          }) {}

  ~Executor() override = default;

  [[nodiscard]] auto Submit(E_ e) {
    return TypeCheck<void>(
        pipe_.Write(std::move(e)));
  }

  [[nodiscard]] auto Shutdown() {
    return TypeCheck<void>(
        pipe_.Close());
  }

  [[nodiscard]] auto InterruptAndShutdown() {
    control_loop_.Interrupt();
    return Shutdown();
  }

  [[nodiscard]] auto Wait() {
    return control_loop_.Wait();
  }

 private:
  std::string name_;
  Pipe<E_> pipe_;
  ControlLoop control_loop_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
