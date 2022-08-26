#pragma once

#include <deque>
#include <optional>

#include "eventuals/callback.hh"
#include "eventuals/just.hh"
#include "eventuals/lock.hh"
#include "eventuals/map.hh"
#include "eventuals/repeat.hh"
#include "eventuals/then.hh"
#include "eventuals/type-check.hh"
#include "eventuals/until.hh"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// A pipe is a queue of values that can be written to and read from.
template <typename T>
class Pipe final : public Synchronizable {
 public:
  Pipe()
    : has_values_or_closed_(&lock()),
      closed_and_empty_((&lock())) {}

  ~Pipe() override = default;

  // Writes a value to the pipe if the pipe is not closed. If the pipe is
  // closed, the value is silently dropped.
  // TODO(benh): Should we raise errors when writing to a closed pipe?
  [[nodiscard]] auto Write(T&& value) {
    return Synchronized(Then([this, value = std::move(value)]() mutable {
      if (!is_closed_) {
        values_.emplace_back(std::move(value));
        has_values_or_closed_.NotifyAll();
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
                     if (is_closed_ && values_.empty()) {
                       closed_and_empty_.NotifyAll();
                     }
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
      has_values_or_closed_.NotifyAll();
      if (values_.empty()) {
        closed_and_empty_.NotifyAll();
      }
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

  // Blocks until the pipe is closed and drained of values.
  // Postcondition: IsClosed() == true && Size() == 0.
  [[nodiscard]] auto WaitForClosedAndEmpty() {
    return TypeCheck<void>(Synchronized(Then([this]() {
      return closed_and_empty_.Wait([this]() {
        return /* while */ !values_.empty() || !is_closed_;
      });
    })));
  }

 private:
  // Notified whenever we either have new values or the pipe has been closed.
  ConditionVariable has_values_or_closed_;
  // Notified once the pipe is closed and is emptied of all values, after which
  // the pipe will never again contain values.
  ConditionVariable closed_and_empty_;
  std::deque<T> values_;
  bool is_closed_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
