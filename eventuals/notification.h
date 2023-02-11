#pragma once

#include "eventuals/lock.h"
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Notification final : public Synchronizable {
 public:
  Notification()
    : notification_(&lock()) {}

  ~Notification() override = default;

  [[nodiscard]] auto Notify() {
    return Synchronized(Then([this]() {
      if (!notified_) {
        notified_ = true;
        notification_.Notify();
      }
    }));
  }

  [[nodiscard]] auto Wait() {
    return Synchronized(Then([this]() {
      return notification_.Wait([this]() {
        return !notified_;
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
