#pragma once

#include "eventuals/callback.hh"
#include "eventuals/catch.hh"
#include "eventuals/finally.hh"
#include "eventuals/lock.hh"
#include "eventuals/promisify.hh"
#include "eventuals/task.hh"
#include "eventuals/type-check.hh"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for running, interrupting, and waiting on a "control loop".
//
// Your control loop MUST not raise any errors or return a value to
// force you to properly handle your errors and store any values
// that you may generate.
//
// A control loop is always automatically started on construction
// and you can interrupt it by calling 'Interrupt()' and wait for it
// to complete by calling 'Wait()'.
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
            >> loop->Synchronized(
                Finally([loop](expected<void, std::exception_ptr>&& expected) {
                  if (!expected) {
                    try {
                      std::rethrow_exception(expected.error());
                    } catch (StoppedException& e) {
                    } catch (std::exception& e) {
                      LOG(WARNING) << "Unreachable: " << e.what();
                    } catch (...) {
                      LOG(WARNING) << "Unreachable (error does not derive from"
                                      " 'std::exception')";
                    }
                  }
                  loop->finished_ = true;
                  loop->wait_until_finished_.Notify();
                }));
      }) {
    // Start the task!
    //
    // NOTE: we borrow ourselves so that we can ensure
    // that the task has really finished before destructing.
    stout::borrowed_ptr<ControlLoop> borrow = Borrow();

    task_.Start(
        std::string(name_), // Copy.
        [borrow = std::move(borrow)]() mutable { borrow.relinquish(); },
        [](std::exception_ptr) { LOG(FATAL) << "Unreachable"; },
        []() { LOG(FATAL) << "Unreachable"; });
  }

  ~ControlLoop() override {
    // Need to wait until there are no more borrows, otherwise we
    // might read an already destructed 'finished_' via the eventual
    // returned from 'Wait()', etc.
    WaitUntilBorrowsEquals(0);
  }

  void Interrupt() {
    task_.Interrupt();
  }

  [[nodiscard]] auto Wait() {
    return TypeCheck<void>(
        Synchronized(
            wait_until_finished_.Wait(Borrow([this]() {
              return /* wait = */ !finished_;
            }))));
  }

 private:
  std::string name_;
  ConditionVariable wait_until_finished_;
  bool finished_ = false;
  Task::Of<void>::With<ControlLoop*> task_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
