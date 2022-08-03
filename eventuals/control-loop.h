#pragma once

#include <atomic>

#include "eventuals/callback.h"
#include "eventuals/catch.h"
#include "eventuals/lock.h"
#include "eventuals/promisify.h"
#include "eventuals/task.h"
#include "eventuals/type-check.h"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for running, interrupting, and waiting on a "control loop".
class ControlLoop final
  : public Synchronizable,
    public stout::enable_borrowable_from_this<ControlLoop> {
 public:
  template <typename F>
  ControlLoop(std::string&& name, F f)
    : Synchronizable(),
      name_(name),
      wait_until_finished_(&lock()),
      task_(this, [f = std::move(f)](ControlLoop* loop) mutable {
        static_assert(
            std::is_invocable_v<F>,
            "'ControlLoop' expects a callable (e.g., a lambda) "
            "that takes no arguments");

        static_assert(
            !HasValueFrom<F>::value,
            "'ControlLoop' expects a callable not an eventual");

        using E = decltype(f());

        static_assert(
            std::is_void_v<E> || HasValueFrom<E>::value,
            "'ControlLoop' expects a callable (e.g., a lambda) that "
            "returns an eventual but you're returning a value");

        static_assert(
            !std::is_void_v<E>,
            "'ControlLoop' expects a callable (e.g., a lambda) that "
            "returns an eventual but you're not returning anything");

        using Value = typename E::template ValueFrom<void>;

        static_assert(
            std::is_void_v<Value>,
            "'ControlLoop' eventual should return 'void'");

        using Errors = typename E::template ErrorsFrom<void, std::tuple<>>;

        static_assert(
            std::is_same_v<Errors, std::tuple<>>,
            "'ControlLoop' eventual should not raise any errors");

        return std::move(f)()
            // NOTE: this catch should not be necessary but until we
            // finish some of the error propagation we have it here
            // just in case.
            >> Catch()
                   .raised<std::exception>([](std::exception&& e) {
                     LOG(FATAL) << e.what();
                   })
            >> loop->Synchronized(
                Then([loop]() {
                  loop->finished_.store(true);
                  loop->wait_until_finished_.Notify();
                }));
      }) {
    // Start the task!
    task_.Start(
        std::string(name_), // Copy.
        []() {},
        [](std::exception_ptr) { LOG(FATAL) << "Unreachable"; },
        []() { LOG(FATAL) << "Unreachable"; });
  }

  ~ControlLoop() override {
    CHECK(finished_.load())
        << "'ControlLoop' must have finished before destruction!";

    // Need to wait until there are no more borrows, otherwise we
    // might read an already destructed 'finished_' via the eventual
    // returned from 'Wait()', etc.
    WaitUntilBorrowsEquals(0);
  }

  [[nodiscard]] auto Interrupt() {
    task_.Interrupt();
  }

  [[nodiscard]] auto Wait() {
    return TypeCheck<void>(
        Synchronized(
            wait_until_finished_.Wait(Borrow([this]() {
              return /* wait = */ !finished_.load();
            }))));
  }

 private:
  std::string name_;
  ConditionVariable wait_until_finished_;
  std::atomic<bool> finished_ = false;
  Task::Of<void>::With<ControlLoop*> task_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
