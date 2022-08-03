#pragma once

#include <atomic>

#include "eventuals/callback.h"
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
      CHECK(that.next_ == nullptr);
    }

    Handler& operator=(const Handler&) = delete;
    Handler& operator=(Handler&&) noexcept = delete;

    ~Handler() = default;

    Interrupt& interrupt() {
      return *CHECK_NOTNULL(interrupt_);
    }

    bool Install(Callback<void()>&& callback) {
      callback_ = std::move(callback);
      return CHECK_NOTNULL(interrupt_)->Install(this);
    }

    bool Install() {
      CHECK(callback_);
      return CHECK_NOTNULL(interrupt_)->Install(this);
    }

    void Invoke() {
      CHECK(callback_);
      callback_();
    }

    Interrupt* interrupt_ = nullptr;
    Callback<void()> callback_;
    Handler* next_ = nullptr;
  };

  Interrupt()
    : handler_(this, []() {}) {}

  bool Install(Handler* handler) {
    CHECK(handler->next_ == nullptr) << "handler is already installed";

    handler->next_ = head_.load(std::memory_order_relaxed);

    do {
      // Check if the interrupt has already been triggered.
      if (handler->next_ == nullptr) {
        return false;
      }
    } while (!head_.compare_exchange_weak(
        handler->next_,
        handler,
        std::memory_order_release,
        std::memory_order_relaxed));

    return true;
  }

  void Trigger() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    auto* handler = head_.exchange(nullptr);
    if (handler != nullptr) {
      while (handler != &handler_) {
        handler->Invoke();
        auto* next = handler->next_;
        handler->next_ = nullptr;
        handler = next;
      }
    }
  }

  bool Triggered() {
    // NOTE: nullptr signifies that the interrupt has been triggered.
    return head_.load() == nullptr;
  }

  // To simplify the implementation we signify a triggered interrupt
  // by storing nullptr in head_. Thus, when an interrupt is first
  // constructed we store a "placeholder" handler that we ignore when
  // executing the rest of the handlers.
  Handler handler_;

  std::atomic<Handler*> head_ = &handler_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
