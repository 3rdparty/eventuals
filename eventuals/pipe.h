#pragma once

#include <deque>
#include <optional>

#include "eventuals/callback.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// A pipe is a queue of values that can be written to and read from.
template <typename T>
class Pipe final : public Synchronizable {
 public:
  Pipe()
    : has_values_or_closed_(&lock()) {}

  ~Pipe() override = default;

  // Writes a value to the pipe if the pipe is not closed. If the pipe is
  // closed, the value is silently dropped.
  // TODO(benh): Should we raise errors when writing to a closed pipe?
  [[nodiscard]] auto Write(T&& value) {
    return Synchronized(Then([this, value = std::move(value)]() mutable {
      if (!is_closed_) {
        values_.emplace_back(std::move(value));
        has_values_or_closed_.Notify();
      }
    }));
  }

  // Reads the next value from the pipe.
  [[nodiscard]] auto Read() {
    return Repeat()
        >> Synchronized(
               Map([this]() {
                 return has_values_or_closed_.Wait([this]() {
                   // Check the condition again in case of spurious wakeups.
                   return values_.empty() && !is_closed_;
                 });
               })
               >> Map([this]() {
                   if (!values_.empty()) {
                     T value = std::move(values_.front());
                     values_.pop_front();
                     return std::make_optional(std::move(value));
                   } else {
                     CHECK(is_closed_);
                     return std::optional<T>();
                   }
                 }))
        >> Until([](std::optional<T>& value) {
             return !value.has_value();
           })
        >> Map([](std::optional<T>&& value) {
             CHECK(value);
             // NOTE: need to use 'Just' here in case 'T' is an
             // eventual otherwise we'll try and compose with it here!
             return Just(std::move(*value));
           });
  }

  // Closes the pipe. Idempotent.
  [[nodiscard]] auto Close() {
    return Synchronized(Then([this]() {
      is_closed_ = true;
      has_values_or_closed_.Notify();
    }));
  }

  // Returns the number of values currently in the pipe.
  [[nodiscard]] auto Size() {
    return Synchronized(Then([this]() {
      return values_.size();
    }));
  }

  // Returns whether the pipe is closed.
  [[nodiscard]] auto IsClosed() {
    return Synchronized(Then([this]() {
      return is_closed_;
    }));
  }

 private:
  // Notified whenever we either have new values or the pipe has been closed.
  ConditionVariable has_values_or_closed_;
  std::deque<T> values_;
  bool is_closed_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
