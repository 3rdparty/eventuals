#pragma once

#include <atomic>

#include "eventuals/callback.h"
#include "glog/logging.h"
#include "include/stout/stateful-tally.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Interrupt final {
 public:
  struct Handler final {
    Handler(Interrupt* interrupt, Callback<void()>&& callback)
      : interrupt_(CHECK_NOTNULL(interrupt)),
        callback_(std::move(callback)) {}

    Handler(Interrupt* interrupt)
      : interrupt_(CHECK_NOTNULL(interrupt)) {}

    Handler(const Handler& that) = delete;

    Handler(Handler&& that) noexcept
      : interrupt_(CHECK_NOTNULL(that.interrupt_)),
        callback_(std::move(that.callback_)) {}

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

    bool Install(Callback<void()>&& callback) {
      callback_ = std::move(callback);
      return interrupt().Install(this);
    }

    bool Install() {
      return interrupt().Install(this);
    }

    void Invoke() {
      callback_();
    }

    Interrupt* interrupt_ = nullptr;
    Callback<void()> callback_;

    enum State : uint8_t {
      UNINSTALLED,
      INSTALLED,
      UNINSTALLING,
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

  bool Install(Handler* handler) {
    if (handler_.load(std::memory_order_relaxed) != nullptr) {
      Handler::State state = handler->state_.state();
      handler->state_.Update(state, Handler::State::INSTALLED);
      handler_.exchange(handler, std::memory_order_relaxed);
      return true;
    } else {
      return false;
    }
  }

  void Uninstall(Handler* handler) {
    Handler::State state = handler->state_.state();
    handler->state_.Update(state, Handler::State::UNINSTALLED);
    // If 'handler' is current 'handler_' then the interrupt *must not*
    // have been triggered so we can set it back to 'placeholder_handler_'.
    handler_.compare_exchange_weak(
        handler,
        &placeholder_handler_,
        std::memory_order_release,
        std::memory_order_relaxed);
  }

  void Trigger() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    auto* handler = handler_.exchange(nullptr);
    if (handler != nullptr) {
      handler->Invoke();
    }

    Interrupt* linked = linked_interrupt_.load(std::memory_order_relaxed);
    if (linked) {
      linked->Trigger();
    }
  }

  bool Triggered() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    return handler_.load() == nullptr;
  }

  Handler placeholder_handler_;
  std::atomic<Handler*> handler_ = &placeholder_handler_;

  std::atomic<Interrupt*> linked_interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
