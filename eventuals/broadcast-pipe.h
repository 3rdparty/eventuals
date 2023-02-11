#pragma once

#include <deque>

#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/on-ended.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T>
class BroadcastPipe final : public Synchronizable {
 public:
  static_assert(
      std::is_copy_constructible_v<T>,
      "'BroadcastPipe' requires copy-constructible values");

  BroadcastPipe()
    : has_values_or_closed_(&lock()) {}

  ~BroadcastPipe() override {
    // TODO(benh): check we're closed and have no more borrowers!
  }

  [[nodiscard]] auto Write(T&& value) {
    return Synchronized(Then([this, value = std::move(value)]() mutable {
      // TODO(benh): raise an error if already closed?
      if (!is_closed_) {
        values_.emplace_back(std::move(value));
        has_values_or_closed_.NotifyAll();
      }
    }));
  }

  [[nodiscard]] auto Read() {
    return Synchronized(Then([this]() {
             readers_++;
             readers_that_still_need_to_read_++;
           }))
        | Repeat()
        | Synchronized(
               OnEnded([this]() {
                 CHECK(lock().OwnedByCurrentSchedulerContext());
                 CHECK_NE(readers_, 0);
                 readers_++;
                 CHECK_NE(readers_that_still_need_to_read_, 0);
                 readers_that_still_need_to_read_++;
               })
               | Map([this]() {
                   return has_values_or_closed_.Wait([this]() {
                     return values_.empty() && !is_closed_;
                   });
                 })
               | Map([this]() {
                   if (!values_.empty()) {
                     if (--readers_that_still_need_to_read_ == 0) {
                       auto value = std::move(values_.front());
                       values_.pop_front();
                       readers_that_still_need_to_read_ = readers_;
                       return std::make_optional(std::move(value));
                     } else {
                       auto value = values_.front();
                       return std::make_optional(std::move(value));
                     }
                   } else {
                     CHECK(is_closed_);
                     return std::optional<T>();
                   }
                 }))
        | Until([](auto& value) {
             return !value.has_value();
           })
        | Map([](auto&& value) {
             CHECK(value);
             // NOTE: need to use 'Just' here in case 'T' is an
             // eventual otherwise we'll try and compose with it here!
             return Just(std::move(*value));
           });
  }

  [[nodiscard]] auto Close() {
    return Synchronized(Then([this]() {
      // TODO(benh): raise an error if already closed?
      is_closed_ = true;
      has_values_or_closed_.NotifyAll();
    }));
  }

  [[nodiscard]] auto Size() {
    return Synchronized(Then([this]() {
      return values_.size();
    }));
  }

 private:
  ConditionVariable has_values_or_closed_;
  std::deque<T> values_;
  bool is_closed_ = false;
  size_t readers_ = 0;
  size_t readers_that_still_need_to_read_ = 0;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
