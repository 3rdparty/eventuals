#pragma once

#include <atomic>

#include "eventuals/callback.h"
#include "glog/logging.h"
#include "stout/stateful-tally.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Interrupt final {
 public:
  struct Handler final {
    enum State : uint8_t {
      UNINSTALLED = 0,
      INSTALLED = 1,
      UNINSTALLING = 2,
      EXECUTED = 3,
    };

    friend std::ostream& operator<<(std::ostream& o, const State& state) {
      switch (state) {
        case UNINSTALLED: return o << "UNINSTALLED";
        case INSTALLED: return o << "INSTALLED";
        case UNINSTALLING: return o << "UNINSTALLING";
        case EXECUTED: return o << "EXECUTED";
      }
    }

    Handler(Interrupt* interrupt, Callback<void()>&& callback)
      : interrupt_(CHECK_NOTNULL(interrupt)),
        callback_(std::move(callback)) {}

    Handler(Interrupt* interrupt)
      : interrupt_(CHECK_NOTNULL(interrupt)) {}

    Handler(const Handler& that) = delete;

    Handler(Handler&& that) noexcept
      : interrupt_(CHECK_NOTNULL(that.interrupt_)),
        callback_(std::move(that.callback_)) {
    }

    ~Handler() {
      Uninstall();
    }

    void Uninstall() {
      State state = state_.state();

      if (state == INSTALLED && state_.Update(state, UNINSTALLING)) {
        if (!interrupt().Uninstall(this)) {
          // The interrupt is uninstalling this handler and
          // we need to signal to them that they can proceed
          // by setting the value back to `INSTALLED` and
          // then wait until that's done.
          state = UNINSTALLING;
          CHECK(state_.Update(state, INSTALLED));
        }
      }

      // Either we performed the `Uninstall` or a thread in `Interrupt` did
      // and we need to wait.
      state_.Wait([](State state, size_t count) {
        return /* until = */ state == UNINSTALLED;
      });
    }

    Interrupt& interrupt() {
      return *CHECK_NOTNULL(interrupt_);
    }

    [[nodiscard]] bool Install(Callback<void()>&& callback) {
      callback_ = std::move(callback);
      return interrupt().Install(this);
    }

    [[nodiscard]] bool Install() {
      CHECK(callback_);
      return interrupt().Install(this);
    }

    State InstallOrExecuteIfTriggered(
        Callback<void()>&& callback) {
      callback_ = std::move(callback);

      if (!interrupt().Install(this)) {
        CHECK_EQ(state_.state(), UNINSTALLED);
        Invoke();
        return EXECUTED;
      }

      return INSTALLED;
    }

    State InstallOrExecuteIfTriggered() {
      CHECK(callback_);

      if (!interrupt().Install(this)) {
        CHECK_EQ(state_.state(), UNINSTALLED);
        Invoke();
        return EXECUTED;
      }

      return INSTALLED;
    }

    void Invoke() {
      CHECK(callback_);
      // Need to move 'callback_' so that it will be destructed from
      // the stack after we've invoked it in case invoking it causes
      // some destructors to run and the callback has any borrows that
      // need to get relinquished.
      Callback<void()> callback = std::move(callback_);
      callback();
    }

    Interrupt* interrupt_ = nullptr;
    Callback<void()> callback_;
    stout::StatefulTally<State> state_ = UNINSTALLED;
  };

  Interrupt()
    : placeholder_handler_(this, []() {}) {}

  Interrupt(Interrupt&& that) = delete;

  ~Interrupt() {
    Handler* handler = handler_.load();

    if (handler != &placeholder_handler_ && handler != nullptr) {
      if (handler_.compare_exchange_weak(
              handler,
              nullptr,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        // We've "won" the race to uninstall the handler but we need to
        // make sure that `~Handler()` isn't simultaneously trying which
        // may end up reading deleted memory if they happen to be
        // context switched out just before they try and use `handler_`
        // themselves. To deal with this case we _wait_ until we can
        // officially do the uninstalling.
        Handler::State state = Handler::State::INSTALLED;
        while (!handler->state_.Update(state, Handler::State::UNINSTALLED)) {
          // TODO(benh): pause CPU.
        }
      } else {
        handler = handler_.load();
        CHECK(handler == nullptr || handler == &placeholder_handler_)
            << "'Interrupt' is being used while destructing!";
      }
    }
  }

  [[nodiscard]] bool Install(Handler* handler) {
    Handler* stored = handler_.load();
    // NOTE: we should be the *only* ones trying to install an interrupt
    // at a time, so the only way we fail to install ourselves is if the
    // interrupt got triggered.
    if (!stored) {
      return false;
    } else if (stored == handler) {
      return true;
    } else {
      Handler::State state = handler->state_.state();
      CHECK_EQ(state, Handler::State::UNINSTALLED);

      bool installed = handler_.compare_exchange_weak(
          stored,
          handler,
          std::memory_order_release,
          std::memory_order_relaxed);

      // 'stored' is 'nullptr' only when race between 'load' and
      // 'compare_exchange_weak' occurs, so 'stored' is not stored
      // in the 'handler_'.
      CHECK(installed || stored == nullptr);

      CHECK(handler->state_.Update(state, Handler::State::INSTALLED));

      if (stored != nullptr) {
        Handler::State state = stored->state_.state();
        while (!stored->state_.Update(state, Handler::State::UNINSTALLED)) {}
      }

      return installed;
    }
    return true;
  }

  [[nodiscard]] bool Uninstall(Handler* handler) {
    // If 'handler' is the current 'handler_' then the interrupt *must not*
    // have been triggered so we want to set 'handler_' back to
    // 'placeholder_handler_' instead of nullptr. If we successfully did the
    // compare-and-swap then that must mean that 'handler' is the current
    // 'handler_' so we "won" the race of uninstalling.
    //
    // Otherwise someone else "won" the compare-and-swap race
    // and they are responsible for setting us to UNINSTALLED.
    if (handler_.compare_exchange_weak(
            handler,
            &placeholder_handler_,
            std::memory_order_release,
            std::memory_order_relaxed)) {
      Handler::State state = handler->state_.state();
      CHECK_EQ(state, Handler::State::UNINSTALLING);
      CHECK(handler->state_.Update(state, Handler::State::UNINSTALLED));
      return true;
    }
    return false;
  }

  void Trigger() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    Handler* handler = handler_.exchange(nullptr);
    if (handler != nullptr && handler != &placeholder_handler_) {
      Handler::State state = handler->state_.state();
      // Need to uninstall the handler so that in the event invoking the
      // callback causes the handler to get destructed it won't try and
      // uninstall itself!
      CHECK_EQ(state, Handler::State::INSTALLED);
      CHECK(handler->state_.Update(state, Handler::State::UNINSTALLED))
          << handler->state_.state();
      handler->Invoke();
    }
  }

  bool Triggered() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    return handler_.load() == nullptr;
  }

  Handler placeholder_handler_;
  std::atomic<Handler*> handler_ = &placeholder_handler_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
