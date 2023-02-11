#pragma once

#include <atomic>
#include <mutex>

#include "eventuals/callback.h"
#include "glog/logging.h"
#include "stout/stateful-tally.h"

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

    Handler(Handler&& that)
      : interrupt_(CHECK_NOTNULL(that.interrupt_)),
        callback_(std::move(that.callback_)) {
      CHECK(that.next_ == nullptr);
    }

    ~Handler() {
      State state = state_.state();
      if (state == INSTALLED && state_.Update(state, UNINSTALLING)) {
        interrupt().Uninstall(this);
      } else {
        // Either we're already uninstalled because we never got
        // installed or the interrupt is uninstalling this handler and
        // we need to wait until that's done.
        state_.Wait([](State state, size_t count) {
          return /* until = */ state == UNINSTALLED;
        });
      }
      CHECK_EQ(next_, nullptr);
      CHECK_EQ(state_.state(), UNINSTALLED);
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
      callback_();
    }

    Interrupt* interrupt_ = nullptr;
    Callback<void()> callback_;
    Handler* next_ = nullptr;

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

  Interrupt() {}

  ~Interrupt() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);

    while (head_ != nullptr) {
      Handler::State state = Handler::INSTALLED;
      if (!head_->state_.Update(state, Handler::UNINSTALLING)) {
        lock.unlock();
        head_->state_.Wait([](Handler::State state, size_t count) {
          return /* until = */ state == Handler::UNINSTALLED;
        });
        lock.lock();
      } else {
        auto* next = head_->next_;
        head_->next_ = nullptr;
        Handler::State state = Handler::UNINSTALLING;
        CHECK(head_->state_.Update(state, Handler::UNINSTALLED));
        head_ = next;
      }
    }
  }

  bool Install(Handler* handler) {
    std::scoped_lock lock(mutex_);

    CHECK_EQ(handler->state_.state(), Handler::UNINSTALLED)
        << "Handler is already installed";

    // Check if the interrupt has already been triggered.
    if (triggered_) {
      return false;
    }

    handler->next_ = head_;

    Handler::State state = Handler::UNINSTALLED;
    CHECK(handler->state_.Update(state, Handler::INSTALLED));

    head_ = handler;

    return true;
  }

  void Uninstall(Handler* handler) {
    std::scoped_lock lock(mutex_);

    CHECK_EQ(handler->state_.state(), Handler::UNINSTALLING);
    CHECK_NE(head_, nullptr) << "Handler should not be installed";

    if (head_ == handler) {
      head_ = handler->next_;

      handler->next_ = nullptr;

      Handler::State state = Handler::UNINSTALLING;
      CHECK(handler->state_.Update(state, Handler::UNINSTALLED));
    } else {
      auto* current = CHECK_NOTNULL(head_);

      while (current->next_ != handler) {
        current = current->next_;
        CHECK_NE(current, nullptr) << "Handler is not installed";
      }

      CHECK_NOTNULL(current)->next_ = handler->next_;

      handler->next_ = nullptr;

      Handler::State state = Handler::UNINSTALLING;
      CHECK(handler->state_.Update(state, Handler::UNINSTALLED));
    }
  }

  void Trigger() {
    std::scoped_lock lock(mutex_);

    if (!triggered_) {
      triggered_ = true;

      while (head_ != nullptr) {
        head_->Invoke();
        auto* next = head_->next_;
        head_->next_ = nullptr;

        Handler::State state = Handler::INSTALLED;
        CHECK(head_->state_.Update(state, Handler::UNINSTALLED));

        head_ = next;
      }
    }
  }

  bool Triggered() {
    std::scoped_lock lock(mutex_);
    // NOTE: nullptr signifies that the interrupt has been triggered.
    return triggered_;
  }

  // TODO(benh): make this lock-free.
  std::recursive_mutex mutex_;

  bool triggered_ = false;
  Handler* head_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
