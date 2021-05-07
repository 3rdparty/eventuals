#pragma once

#include <deque>
#include <mutex>

#include "stout/eventual.h"

namespace stout {
namespace eventuals {

class Lock
{
public:
  template <typename F>
  void Acquire(void* holder, F&& f)
  {
    bool acquired = false;

    mutex_.lock();
    {
      if (holder_ != nullptr) {
        waiters_.emplace_back(Waiter { holder, std::forward<F>(f) });
      } else {
        holder_ = holder;
        acquired = true;
      }
    }
    mutex_.unlock();

    if (acquired) {
      f();
    }
  }

  void Release()
  {
    Waiter waiter;

    mutex_.lock();
    {
      if (!waiters_.empty()) {
        waiter = std::move(waiters_.front());
        waiters_.pop_front();
        holder_ = waiter.holder_;
      } else {
        holder_ = nullptr;
      }
    }
    mutex_.unlock();

    if (waiter) {
      waiter.f_();
    }
  }

  void* holder()
  {
    return holder_;
  }

private:
  std::mutex mutex_;

  void* holder_ = nullptr;

  struct Waiter
  {
    void* holder_ = nullptr;
    std::function<void()> f_;
    operator bool() { return holder_ != nullptr; }
  };

  std::deque<Waiter> waiters_;
};

namespace detail {

template <typename F, typename... Values>
struct ResultOfPossibleUndefined
{
  using type = std::result_of_t<F(Values...)>;
};


template <typename F>
struct ResultOfPossibleUndefined<F, Undefined>
{
  using type = std::result_of_t<F()>;
};


template <typename K, typename Value>
struct ValueFrom
{
  using type = typename K::Value;
};


template <typename Value>
struct ValueFrom<Undefined, Value>
{
  using type = Value;
};


template <typename K_, typename Value_>
struct Acquire
{
  using Value = typename ValueFrom<K_, Value_>::type;

  Acquire(K_ k, Lock* lock) : k_(std::move(k)), lock_(lock) {}

  template <typename Value, typename K>
  static auto create(K k, Lock* lock)
  {
    return Acquire<K, Value>(std::move(k), lock);
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
   return create<Value>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        lock_);
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
   using Value = typename ResultOfPossibleUndefined<F, Value_>::type;

    return std::move(*this).k(
        eventuals::Eventual<Value>()
        .context(std::move(f))
        .start([](auto& f, auto& k, auto&&... args) {
          eventuals::succeed(k, f(std::forward<decltype(args)>(args)...));
        })
        .fail([](auto&, auto& k, auto&&... args) {
          eventuals::fail(k, std::forward<decltype(args)>(args)...);
        })
        .stop([](auto&, auto& k) {
          eventuals::stop(k);
        }));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    // TODO(benh): "schedule" the lambda to run on the current thread
    // pool if the current thread is part of a thread pool.
    //
    // TODO(benh): Only copy the args if can't acquire the lock.
    lock_->Acquire(
        this,
        [this,
         tuple = std::forward_as_tuple(std::forward<Args>(args)...)]() mutable {
          std::apply([&](auto&&... args) {
            eventuals::succeed(k_, std::forward<decltype(args)>(args)...);
          },
          std::move(tuple));
        });
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): "schedule" the lambda to run on the current thread
    // pool if the current thread is part of a thread pool.
    //
    // TODO(benh): Only copy the args if can't acquire the lock.
    lock_->Acquire(
        this,
        [this,
         tuple = std::forward_as_tuple(std::forward<Args>(args)...)]() mutable {
          std::apply([&](auto&&... args) {
            eventuals::fail(k_, std::forward<decltype(args)>(args)...);
          },
          std::move(tuple));
        });
  }

  void Stop()
  {
    if (lock_->holder() != this) {
      // TODO(benh): "schedule" the lambda to run on the current thread
      // pool if the current thread is part of a thread pool.
      lock_->Acquire(
          this,
          [this]() mutable {
            eventuals::stop(k_);
          });
    } else {
      eventuals::stop(k_);
    }
  }

  K_ k_;
  Lock* lock_;
};


template <typename K_, typename Value_>
struct Release
{
  using Value = typename ValueFrom<K_, Value_>::type;

  Release(K_ k, Lock* lock) : k_(std::move(k)), lock_(lock) {}

  template <typename Value, typename K>
  static auto create(K k, Lock* lock)
  {
    return Release<K, Value>(std::move(k), lock);
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
   return create<Value>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        lock_);
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    using Value = typename ResultOfPossibleUndefined<F, Value_>::type;

    return std::move(*this).k(
        eventuals::Eventual<Value>()
        .context(std::move(f))
        .start([](auto& f, auto& k, auto&&... args) {
          eventuals::succeed(k, f(std::forward<decltype(args)>(args)...));
        })
        .fail([](auto&, auto& k, auto&&... args) {
          eventuals::fail(k, std::forward<decltype(args)>(args)...);
        })
        .stop([](auto&, auto& k) {
          eventuals::stop(k);
        }));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    lock_->Release();
    eventuals::succeed(k_, std::forward<decltype(args)>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    lock_->Release();
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    lock_->Release();
    eventuals::stop(k_);
  }

  K_ k_;
  Lock* lock_;
};

} // namespace detail {

template <typename K, typename Value>
struct IsContinuation<
  detail::Acquire<K, Value>> : std::true_type {};


template <typename K, typename Value>
struct HasTerminal<
  detail::Acquire<K, Value>> : HasTerminal<K> {};


template <typename K, typename Value_>
struct Compose<detail::Acquire<K, Value_>>
{
  template <typename Value>
  static auto compose(detail::Acquire<K, Value_> acquire)
  {
    return detail::Acquire<K, Value>(std::move(acquire.k_), acquire.lock_);
  }
};


template <typename K, typename Value_>
struct Compose<detail::Release<K, Value_>>
{
  template <typename Value>
  static auto compose(detail::Release<K, Value_> acquire)
  {
    return detail::Release<K, Value>(std::move(acquire.k_), acquire.lock_);
  }
};



template <typename K, typename Value>
struct IsContinuation<
  detail::Release<K, Value>> : std::true_type {};


template <typename K, typename Value>
struct HasTerminal<
  detail::Release<K, Value>> : HasTerminal<K> {};


inline auto Acquire(Lock* lock)
{
  return detail::Acquire<Undefined, Undefined>(Undefined(), lock);
}


inline auto Release(Lock* lock)
{
  return detail::Release<Undefined, Undefined>(Undefined(), lock);
}


class Synchronizable
{
public:
  Synchronizable(Lock* lock) : lock_(lock) {}

  template <typename E>
  auto synchronized(E e)
  {
    return Acquire(lock_)
      | std::move(e)
      | Release(lock_);
  }

private:
  Lock* lock_;
};

} // namespace eventuals {
} // namespace stout {
