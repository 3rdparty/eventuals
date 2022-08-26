#pragma once

#include "eventuals/lock.hh"
#include "eventuals/then.hh"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// This header file defines a 'Notification' abstraction, which allows
// an eventual to be able to wait for a single occurrence of a single
// event. Said another way, the 'Notification' object maintains a
// private boolean "notified" that transitions to 'true' at most once.
//
// This is similar to 'absl::Notification'
// (https://abseil.io/docs/cpp/guides/synchronization#notification).
//
// You can call 'Wait()' on a 'Notification' to wait until the
// "notified" state is 'true' and 'Notify()' to set the notification's
// "notified" state to 'true' and notify all waiting eventuals that
// the event has occurred. This method may only be called once and
// will abort if called more than once.
//
// Note that while 'Notify()' should only be called once, it is
// perfectly valid to call 'Wait()' multiple times and/or from
// multiple eventuals -- even after the notification's "notified"
// state has been set -- in which case those calls will not wait.
//
// Note that the lifetime of a 'Notification' requires careful
// consideration; it might not be safe to destroy a 'Notification'
// after calling 'Notify()' since other eventuals may have called, or
// will call, 'Wait()'.
class Notification final : public Synchronizable {
 public:
  Notification()
    : notification_(&lock()) {}

  // Sets "notified" to true and notifies waiting eventuals.
  //
  // Should only be called once. Repeated calls result in an abort.
  //
  // Returns an eventual 'void'.
  [[nodiscard]] auto Notify() {
    return Synchronized(Then([this]() {
      CHECK(!notified_)
          << "'Notification' can not be notified more than once";
      notified_ = true;
      notification_.NotifyAll();
    }));
  }

  // Waits until "notified" is true, which might already be the case
  // if 'Notify()' has been called.
  //
  // Returns an eventual 'void'.
  [[nodiscard]] auto WaitForNotification() {
    return Synchronized(Then([this]() {
      return notification_.Wait([this]() {
        return /* while = */ !notified_;
      });
    }));
  }

 private:
  ConditionVariable notification_;
  bool notified_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
