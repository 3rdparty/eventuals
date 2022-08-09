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
    };

    friend std::ostream& operator<<(std::ostream& o, const State& state) {
      if (state == UNINSTALLED) {
        return o << "UNINSTALLED";

      } else if (state == INSTALLED) {
        return o << "INSTALLED";

      } else {
        CHECK_EQ(state, UNINSTALLING);
        return o << "UNINSTALLING";
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
      State state = state_.state();

      if (state == INSTALLED && state_.Update(state, UNINSTALLING)) {
        interrupt().Uninstall(this);
      }

      // Either we're already uninstalled because we never got
      // installed or the interrupt is uninstalling this handler and
      // we need to wait until that's done.
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

    void Invoke() {
      CHECK(callback_);
      callback_();
    }

    Interrupt* interrupt_ = nullptr;
    Callback<void()> callback_;
    stout::StatefulTally<State> state_ = UNINSTALLED;
  };

  Interrupt()
    : placeholder_handler_(this, []() {}) {}

  ~Interrupt() {
    auto* handler =
        handler_.exchange(&placeholder_handler_, std::memory_order_relaxed);
    if (handler != nullptr) {
      Handler::State state = handler->state_.state();
      handler->state_.Update(state, Handler::State::UNINSTALLED);
    }
  }

  [[nodiscard]] bool Install(Handler* handler) {
    Handler* stored = handler_.load();
    // NOTE: we should be the *only* ones trying to install an interrupt
    // at a time, so the only way we fail to install ourselves is if the
    // interrupt got triggered.
    if (!stored) {
      return false;
    }

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

    return installed;
  }

  void Uninstall(Handler* handler) {
    Handler::State state = handler->state_.state();
    CHECK_EQ(state, Handler::State::UNINSTALLING);

    // If 'handler' is current 'handler_' then the interrupt *must not*
    // have been triggered so we can set it back to 'placeholder_handler_'.
    if (handler_.load() == handler) {
      handler_.exchange(&placeholder_handler_);
    }
    CHECK(handler->state_.Update(state, Handler::State::UNINSTALLED));
  }

  void Trigger() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    Handler* handler = handler_.exchange(nullptr);
    if (handler != nullptr) {
      // To be sure that callback won't be invoked while destructing.
      CHECK_NE(handler->state_.state(), Handler::State::UNINSTALLING);
      handler->Invoke();
    }
  }

  bool Triggered() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    return handler_.load() == nullptr;
  }

  bool Installed() {
    return handler_.load() != &placeholder_handler_;
  }

  Handler placeholder_handler_;
  std::atomic<Handler*> handler_ = &placeholder_handler_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
