#pragma once

#include <atomic>
#include <optional>

#include "stout/eventual.h"
#include "stout/callback.h"

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename K>
void notify(K& k)
{
  k.Notify();
}


template <typename K>
void wait(K& k)
{
  k.Wait();
}

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
          eventuals::succeed(k_, std::move(*value_));
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

template <typename Wait_>
struct WaitK
{
  Wait_* wait_ = nullptr;

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(wait_->k_, std::forward<Args>(args)...);
  }

  void Notify()
  {
    if (!wait_->wait_) {
      throw std::runtime_error("Attempting to notify without waiting");
    }

    wait_->lock_->AcquireSlow(&wait_->waiter_);
  }

  void Wait()
  {
    wait_->wait_ = true;
  }
};


template <
  typename K_,
  typename Context_,
  typename Condition_,
  typename Arg_,
  typename Value_>
struct Wait
{
  using Value = Value_;

  Wait(K_ k, Context_ context, Condition_ condition, Lock* lock)
    : k_(std::move(k)),
      context_(std::move(context)),
      condition_(std::move(condition)),
      lock_(lock) {}

  Wait(Wait&& that)
    : k_(std::move(that.k_)),
      context_(std::move(that.context_)),
      condition_(std::move(that.condition_)),
      lock_(that.lock_) {}

  template <
    typename Arg,
    typename Value,
    typename K,
    typename Context,
    typename Condition>
  static auto create(K k, Context context, Condition condition, Lock* lock)
  {
    return Wait<K, Context, Condition, Arg, Value>(
        std::move(k),
        std::move(context),
        std::move(condition),
        lock);
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value
      || !IsUndefined<K_>::value, int> = 0>
  auto k(K k) &&
  {
   return create<Arg_, Value_>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(context_),
        std::move(condition_),
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

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Arg_, Value_>(
        std::move(k_),
        std::move(context),
        std::move(condition_),
        lock_);
  }

  template <typename Condition>
  auto condition(Condition condition) &&
  {
    static_assert(IsUndefined<Condition_>::value, "Duplicate 'condition'");
    return create<Arg_, Value_>(
        std::move(k_),
        std::move(context_),
        std::move(condition),
        lock_);
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    static_assert(
        !IsUndefined<Condition_>::value,
        "Undefined 'condition' (and no default)");

    waitk_.wait_ = this;

    if constexpr (IsUndefined<Context_>::value) {
      condition_(waitk_, std::forward<Args>(args)...);
    } else {
      condition_(context_, waitk_, std::forward<Args>(args)...);
    }

    if (wait_) {
      static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                    "Wait only supports 0 or 1 argument, but found > 1");

      static_assert(IsUndefined<Value_>::value || sizeof...(args) == 1);

      if constexpr (sizeof...(args) == 1) {
        assert(!arg_);
        arg_.emplace(std::forward<Args>(args)...);
      }

      waiter_.f_ = [this]() mutable {
        wait_ = false;

        // TODO(benh): submit to run on the *current* thread pool.
        if constexpr (sizeof...(args) == 1) {
          Start(std::move(*arg_));
        } else {
          Start();
        }
      };

      lock_->Release();
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): allow override of 'fail'.
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    // TODO(benh): allow override of 'stop'.
    eventuals::stop(k_);
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;
  Context_ context_;
  Condition_ condition_;
  Lock* lock_;
  Lock::Waiter waiter_;
  std::optional<Arg_> arg_;
  bool wait_ = false;
  WaitK<Wait> waitk_;
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

////////////////////////////////////////////////////////////////////////

inline auto Acquire(Lock* lock)
{
  return detail::Acquire<Undefined, Undefined>(Undefined(), lock);
}

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct IsContinuation<
  detail::Release<K, Value>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct HasTerminal<
  detail::Release<K, Value>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

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

inline auto Release(Lock* lock)
{
  return detail::Release<Undefined, Undefined>(Undefined(), lock);
}

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Condition,
  typename Arg,
  typename Value>
struct IsContinuation<
  detail::Wait<K, Context, Condition, Arg, Value>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Condition,
  typename Arg,
  typename Value>
struct HasTerminal<
  detail::Wait<K, Context, Condition, Arg, Value>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Condition,
  typename Arg_,
  typename Value>
struct Compose<detail::Wait<K, Context, Condition, Arg_, Value>>
{
  template <typename Arg>
  static auto compose(detail::Wait<K, Context, Condition, Arg_, Value> wait)
  {
    return detail::Wait<K, Context, Condition, Arg, Value>(
        std::move(wait.k_),
        std::move(wait.context_),
        std::move(wait.condition_),
        wait.lock_);
  }
};

////////////////////////////////////////////////////////////////////////

template <typename Value>
auto Wait(Lock* lock)
{
  return detail::Wait<Undefined, Undefined, Undefined, Undefined, Value>(
      Undefined(),
      Undefined(),
      Undefined(),
      lock);
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
