#include <deque>

#include "eventuals/callback.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T>
class Pipe : public Synchronizable {
 public:
  auto Write(T&& value) {
    return Synchronized(Then([this, value = std::move(value)]() mutable {
      if (!is_closed_) {
        values_.emplace_back(std::move(value));
        notify_();
      }
    }));
  }

  auto Read() {
    return Repeat()
        | Synchronized(
               Wait([this](auto notify) {
                 notify_ = std::move(notify);
                 return [this]() {
                   return values_.empty() && !is_closed_;
                 };
               })
               | Map([this]() {
                   if (!values_.empty()) {
                     auto value = std::move(values_.front());
                     values_.pop_front();
                     return std::make_optional(std::move(value));
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
             return std::move(*value);
           });
  }

  auto Close() {
    return Synchronized(Then([this]() {
      is_closed_ = true;
      notify_();
    }));
  }

  auto Size() {
    return Synchronized(Then([this]() {
      return values_.size();
    }));
  }

 private:
  Callback<> notify_ = []() {};
  std::deque<T> values_;
  bool is_closed_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
