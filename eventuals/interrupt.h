#pragma once

#include <atomic>

#include "eventuals/callback.h"
#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Interrupt {
 public:
  struct Handler {
    template <typename F>
    Handler(Interrupt* interrupt, F f)
      : interrupt_(CHECK_NOTNULL(interrupt)),
        f_(std::move(f)) {
    }

    Handler(const Handler& that) = delete;

    Handler(Handler&& that)
      : interrupt_(that.interrupt_),
        f_(std::move(that.f_)) {
      CHECK(that.next_ == nullptr);
    }

    bool Install() {
      CHECK_NOTNULL(interrupt_);
      return interrupt_->Install(this);
    }

    void Invoke() {
      CHECK(f_);
      f_();
    }

    Interrupt* interrupt_;
    Callback<> f_;
    Handler* next_ = nullptr;
  };

  Interrupt()
    : handler_(this, []() {}) {}

  bool Install(Handler* handler) {
    CHECK(handler->next_ == nullptr);

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
