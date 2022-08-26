#pragma once

#include <atomic>

#include "eventuals/callback.hh"
#include "glog/logging.h"

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
        callback_(std::move(that.callback_)) {
    }

    Interrupt& interrupt() {
      return *CHECK_NOTNULL(interrupt_);
    }

    bool Install(Callback<void()>&& callback) {
      callback_ = std::move(callback);
      return interrupt().Install(this);
    }

    bool Install() {
      CHECK(callback_);
      return interrupt().Install(this);
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
  };

  Interrupt()
    : placeholder_handler_(this, []() {}) {}

  [[nodiscard]] bool Install(Handler* handler) {
    Handler* stored = handler_.load();
    // NOTE: we should be the *only* ones trying to install an interrupt
    // at a time, so the only way we fail to install ourselves is if the
    // interrupt got triggered.
    if (!stored) {
      return false;
    }

    bool installed = handler_.compare_exchange_weak(
        stored,
        handler,
        std::memory_order_release,
        std::memory_order_relaxed);

    // 'stored' might be a 'nullptr' only in case of race betweer
    // 'load' and 'compare_exchange_weak'.
    CHECK(installed || stored == nullptr);
    return installed;
  }

  void Trigger() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    Handler* handler = handler_.exchange(nullptr);
    if (handler != nullptr) {
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
