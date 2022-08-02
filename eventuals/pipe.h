#pragma once

#include <deque>

#include "eventuals/callback.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/type-check.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T>
class Pipe final : public Synchronizable {
 public:
  Pipe()
    : has_values_or_closed_(&lock()),
      closed_and_empty_((&lock())) {}

  ~Pipe() override = default;

  [[nodiscard]] auto Write(T&& value) {
    return Synchronized(Then([this, value = std::move(value)]() mutable {
      if (!is_closed_) {
        values_.emplace_back(std::move(value));
        has_values_or_closed_.Notify();
      }
    }));
  }

  [[nodiscard]] auto Read() {
    return Repeat()
        >> Synchronized(
               Map([this]() {
                 return has_values_or_closed_.Wait([this]() {
                   return values_.empty() && !is_closed_;
                 });
               })
               >> Map([this]() {
                   if (!values_.empty()) {
                     auto value = std::move(values_.front());
                     values_.pop_front();
                     if (is_closed_ && values_.empty()) {
                       closed_and_empty_.Notify();
                     }
                     return std::make_optional(std::move(value));
                   } else {
                     CHECK(is_closed_);
                     return std::optional<T>();
                   }
                 }))
        >> Until([](auto& value) {
             return !value.has_value();
           })
        >> Map([](auto&& value) {
             CHECK(value);
             // NOTE: need to use 'Just' here in case 'T' is an
             // eventual otherwise we'll try and compose with it here!
             return Just(std::move(*value));
           });
  }

  [[nodiscard]] auto Close() {
    return Synchronized(Then([this]() {
      is_closed_ = true;
      has_values_or_closed_.Notify();
      if (values_.empty()) {
        closed_and_empty_.Notify();
      }
    }));
  }

  [[nodiscard]] auto Size() {
    return Synchronized(Then([this]() {
      return values_.size();
    }));
  }

  [[nodiscard]] auto IsClosed() {
    return Synchronized(Then([this]() {
      return is_closed_;
    }));
  }

  // Blocks until the pipe is closed and drained of values.
  // Postcondition: IsClosed() == true && Size() == 0.
  [[nodiscard]] auto WaitForClosedAndEmpty() {
    return TypeCheck<void>(Synchronized(Then(
        [this]() { return closed_and_empty_.Wait(); })));
  }

 private:
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
