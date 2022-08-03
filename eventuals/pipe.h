#pragma once

#include <deque>

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

template <typename T>
class Pipe final : public Synchronizable {
 public:
  Pipe()
    : has_values_or_closed_(&lock()) {}

  Pipe(const Pipe&) = default;
  Pipe(Pipe&&) noexcept = default;

  Pipe& operator=(const Pipe&) = default;
  Pipe& operator=(Pipe&&) noexcept = default;

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

 private:
  ConditionVariable has_values_or_closed_;
  std::deque<T> values_;
  bool is_closed_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
