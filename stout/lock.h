#pragma once

#include <atomic>
#include <optional>

#include "stout/eventual.h"
#include "stout/callback.h"

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Lock
{
public:
  struct Waiter
  {
    Callback<> f_;
    Waiter* next_ = nullptr;
  };

  bool AcquireFast(Waiter* waiter)
  {
    assert(waiter->next_ == nullptr);

    waiter->next_ = head_.load(std::memory_order_relaxed);

    while (waiter->next_ == nullptr) {
      if (head_.compare_exchange_weak(
              waiter->next_, waiter,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        return true;
      }
    }

    waiter->next_ = nullptr;

    return false;
  }

  void AcquireSlow(Waiter* waiter)
  {
    assert(waiter->next_ == nullptr);

    waiter->next_ = head_.load(std::memory_order_relaxed);

    while (!head_.compare_exchange_weak(
               waiter->next_, waiter,
               std::memory_order_release,
               std::memory_order_relaxed));

    // Check whether we *acquired* (i.e., even though this is the slow
    // path it's possible that the lock was held in the fast path and
    // was released before trying the slow path, hence we might have
    // been able to acquire) or *queued* (i.e., we have some waiters
    // ahead of us). If we acquired then execute the waiter's
    // continuation.
    if (waiter->next_ == nullptr) {
      waiter->f_();
    }
  }

  void Release()
  {
    auto* waiter = head_.load(std::memory_order_relaxed);

    // Should have at least one waiter (who ever acquired) even if
    // they're aren't any others waiting.
    assert(waiter != nullptr);

    if (waiter->next_ == nullptr) {
      if (!head_.compare_exchange_weak(
              waiter, nullptr,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        return Release(); // Try again.
      }
    } else {
      while (waiter->next_->next_ != nullptr) {
        waiter = waiter->next_;
      }
      waiter->next_ = nullptr;
      waiter->f_();
    }
  }

private:
  std::atomic<Waiter*> head_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename F, typename... Values>
struct InvokeResultPossiblyUndefined
{
  using type = std::invoke_result_t<F, Values...>;
};

template <typename F>
struct InvokeResultPossiblyUndefined<F, Undefined>
{
  using type = std::invoke_result_t<F>;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Value_>
struct Acquire
{
  using Value = typename ValueFrom<K_, Value_>::type;

  Acquire(K_ k, Lock* lock)
    : k_(std::move(k)), lock_(lock) {}

  template <typename Value, typename K>
  static auto create(K k, Lock* lock)
  {
    return Acquire<K, Value>(std::move(k), lock);
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value
      || !IsUndefined<K_>::value, int> = 0>
  auto k(K k) &&
  {
   return create<Value_>(
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
      !IsContinuation<F>::value
      && IsUndefined<K_>::value, int> = 0>
  auto k(F f) &&
  {
    using Result = typename InvokeResultPossiblyUndefined<F, Value>::type;
    return std::move(*this).k(
        eventuals::Eventual<Result>()
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
    if (lock_->AcquireFast(&waiter_)) {
      eventuals::succeed(k_, std::forward<Args>(args)...);
    } else {
      static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                    "Acquire only supports 0 or 1 argument, but found > 1");

      static_assert(IsUndefined<Value_>::value || sizeof...(args) == 1);

      if constexpr (sizeof...(args) == 1) {
        assert(!value_);
        value_.emplace(std::forward<Args>(args)...);
      }

      waiter_.f_ = [this]() mutable {
        // TODO(benh): submit to run on the *current* thread pool.
        if constexpr (sizeof...(args) == 1) {
          eventuals::succeed(k_, *value_);
        } else {
          eventuals::succeed(k_);
        }
      };

      lock_->AcquireSlow(&waiter_);
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if (lock_->AcquireFast(&waiter_)) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else {
      // TODO(benh): avoid allocating in the heap by storing args in
      // pre-allocated buffer based on tracking Errors...
      waiter_.f_ = [
          this,
          tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        // TODO(benh): submit to run on the *current* thread pool.
        std::apply([&](auto&&... args) {
          eventuals::fail(k_, std::forward<decltype(args)>(args)...);
        },
        std::move(tuple));
      };
      lock_->AcquireSlow(&waiter_);
    }
  }

  void Stop()
  {
    if (lock_->AcquireFast(&waiter_)) {
      eventuals::stop(k_);
    } else {
      waiter_.f_ = [this]() mutable {
        // TODO(benh): submit to run on the *current* thread pool.
        eventuals::stop(k_);
      };
      lock_->AcquireSlow(&waiter_);
    }
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;
  Lock* lock_;
  Lock::Waiter waiter_;
  std::optional<Value_> value_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Value_>
struct Release
{
  using Value = typename ValueFrom<K_, Value_>::type;

  Release(K_ k, Lock* lock)
    : k_(std::move(k)), lock_(lock) {}

  Release(Release&& that)
    : k_(std::move(that.k_)), lock_(that.lock_) {}

  template <typename Value, typename K>
  static auto create(K k, Lock* lock)
  {
    return Release<K, Value>(std::move(k), lock);
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value
      || !IsUndefined<K_>::value, int> = 0>
  auto k(K k) &&
  {
   return create<Value_>(
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
      !IsContinuation<F>::value
      && IsUndefined<K_>::value, int> = 0>
  auto k(F f) &&
  {
    using Result = typename InvokeResultPossiblyUndefined<F, Value>::type;
    return std::move(*this).k(
        eventuals::Eventual<Result>()
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
    // NOTE: only one of Start, Fail, or Stop *should* be getting
    // called but being conservative here (for now).
    bool expected = false;
    if (released_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_release,
            std::memory_order_relaxed)) {
      lock_->Release();
      eventuals::succeed(k_, std::forward<decltype(args)>(args)...);
    } else {
      std::abort();
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // NOTE: only one of Start, Fail, or Stop *should* be getting
    // called but being conservative here (for now).
    bool expected = false;
    if (released_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_release,
            std::memory_order_relaxed)) {
      lock_->Release();
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else {
      std::abort();
    }
  }

  void Stop()
  {
    // NOTE: only one of Start, Fail, or Stop *should* be getting
    // called but being conservative here (for now).
    bool expected = false;
    if (released_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_release,
            std::memory_order_relaxed)) {
      lock_->Release();
      eventuals::stop(k_);
    } else {
      std::abort();
    }
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;
  Lock* lock_;
  std::atomic<bool> released_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct IsContinuation<
  detail::Acquire<K, Value>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct HasTerminal<
  detail::Acquire<K, Value>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

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
  static auto compose(detail::Release<K, Value_> release)
  {
    return detail::Release<K, Value>(std::move(release.k_), release.lock_);
  }
};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct IsContinuation<
  detail::Release<K, Value>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct HasTerminal<
  detail::Release<K, Value>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

inline auto Acquire(Lock* lock)
{
  return detail::Acquire<Undefined, Undefined>(Undefined(), lock);
}

////////////////////////////////////////////////////////////////////////

inline auto Release(Lock* lock)
{
  return detail::Release<Undefined, Undefined>(Undefined(), lock);
}

////////////////////////////////////////////////////////////////////////

class Synchronizable
{
public:
  Synchronizable(Lock* lock) : lock_(lock) {}

  template <typename E>
  auto synchronized(E e) const
  {
    return Acquire(lock_)
      | std::move(e)
      | Release(lock_);
  }

private:
  Lock* lock_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {
