#pragma once

#include <atomic>
#include <optional>

#include "stout/eventual.h"
#include "stout/callback.h"
#include "stout/invoke-result.h"
#include "stout/lambda.h"
#include "stout/then.h"
#include "stout/scheduler.h"

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
    Callback<> f;
    Waiter* next = nullptr;
    bool acquired = false;
  };

  bool AcquireFast(Waiter* waiter)
  {
    CHECK(!waiter->acquired) << "recursive lock acquire detected";
    CHECK(waiter->next == nullptr);

    waiter->next = head_.load(std::memory_order_relaxed);

    while (waiter->next == nullptr) {
      if (head_.compare_exchange_weak(
              waiter->next, waiter,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        waiter->acquired = true;
        return true;
      }
    }

    waiter->next = nullptr;

    return false;
  }

  bool AcquireSlow(Waiter* waiter)
  {
    CHECK(!waiter->acquired) << "recursive lock acquire detected";
    CHECK(waiter->next == nullptr);

    waiter->next = head_.load(std::memory_order_relaxed);

    while (!head_.compare_exchange_weak(
               waiter->next, waiter,
               std::memory_order_release,
               std::memory_order_relaxed));

    // Check whether we *acquired* (i.e., even though this is the slow
    // path it's possible that the lock was held in the fast path and
    // was released before trying the slow path, hence we might have
    // been able to acquire) or *queued* (i.e., we have some waiters
    // ahead of us).
    if (waiter->next == nullptr) {
      waiter->acquired = true;
      return true;
    }

    return false;
  }

  void Release()
  {
    auto* waiter = head_.load(std::memory_order_relaxed);

    // Should have at least one waiter (who ever acquired) even if
    // they're aren't any others waiting.
    CHECK_NOTNULL(waiter);

    if (waiter->next == nullptr) {
      if (!head_.compare_exchange_weak(
              waiter, nullptr,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        return Release(); // Try again.
      }
      waiter->acquired = false;
    } else {
      while (waiter->next->next != nullptr) {
        waiter = waiter->next;
      }

      waiter->next->acquired = false;
      // std::cout << "Released (more)" << std::endl;
      waiter->next = nullptr;
      waiter->acquired = true;
      waiter->f();
    }
  }

  bool Available()
  {
    return head_.load(std::memory_order_relaxed) == nullptr;
  }

private:
  std::atomic<Waiter*> head_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

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
      IsContinuation<K>::value, int> = 0>
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
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    return std::move(*this) | eventuals::Lambda(std::move(f));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    scheduler_ = Scheduler::Get(&context_);

    STOUT_EVENTUALS_LOG(1) << "'" << context_->name() << "' acquiring";

    if (lock_->AcquireFast(&waiter_)) {
      STOUT_EVENTUALS_LOG(1) << "'" << context_->name() << "' (fast) acquired";
      eventuals::succeed(k_, std::forward<Args>(args)...);
    } else {
      static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                    "Acquire only supports 0 or 1 argument, but found > 1");

      static_assert(IsUndefined<Value_>::value || sizeof...(args) == 1);

      if constexpr (sizeof...(args) == 1) {
        value_.emplace(std::forward<Args>(args)...);
      }

      waiter_.f = [this]() mutable {
        STOUT_EVENTUALS_LOG(1)
          << "'" << context_->name() << "' (very slow) acquired";

        scheduler_->Submit(
            [this]() mutable {
              if constexpr (sizeof...(args) == 1) {
                eventuals::succeed(k_, std::move(*value_));
              } else {
                eventuals::succeed(k_);
              }
            },
            context_);
      };

      if (lock_->AcquireSlow(&waiter_)) {
        STOUT_EVENTUALS_LOG(1)
          << "'" << context_->name() << "' (slow) acquired";

        if constexpr (sizeof...(args) == 1) {
          eventuals::succeed(k_, std::move(*value_));
        } else {
          eventuals::succeed(k_);
        }
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    scheduler_ = Scheduler::Get(&context_);

    if (lock_->AcquireFast(&waiter_)) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else {
      // TODO(benh): avoid allocating on heap by storing args in
      // pre-allocated buffer based on composing with Errors.
      auto* tuple = new std::tuple { this, std::forward<Args>(args)... };

      waiter_.f = [tuple]() mutable {
        std::apply([tuple](auto* acquire, auto&&...) {
          auto* scheduler_ = acquire->scheduler_;
          auto* context_ = acquire->context_;
          scheduler_->Submit(
              [tuple]() mutable {
                std::apply([](auto* acquire, auto&&... args) {
                  auto& k_ = *acquire->k_;
                  eventuals::fail(k_, std::forward<decltype(args)>(args)...);
                },
                std::move(*tuple));
                delete tuple;
              },
              context_);
        },
        std::move(*tuple));
      };

      if (lock_->AcquireSlow(&waiter_)) {
        waiter_.f();
      }
    }
  }

  void Stop()
  {
    scheduler_ = Scheduler::Get(&context_);

    if (lock_->AcquireFast(&waiter_)) {
      eventuals::stop(k_);
    } else {
      waiter_.f = [this]() mutable {
        scheduler_->Submit(
            [this]() mutable {
              eventuals::stop(k_);
            },
            context_);
      };

      if (lock_->AcquireSlow(&waiter_)) {
        waiter_.f();
      }
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
  Scheduler* scheduler_ = nullptr;
  Scheduler::Context* context_ = nullptr;
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
      IsContinuation<K>::value, int> = 0>
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
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    return std::move(*this) | eventuals::Lambda(std::move(f));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    CHECK(!lock_->Available());
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

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;
  Lock* lock_;
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

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(wait_->k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(wait_->k_);
  }

  void Notify()
  {
    // NOTE: we ignore notifications unless we're notifiable and we
    // make sure we're not notifiable after the first notification so
    // we don't try and add ourselves to the list of waiters again.
    //
    // TODO(benh): make sure *we've* acquired the lock (where 'we' is
    // the current "eventual").
    if (wait_->notifiable_) {
      CHECK(!wait_->lock_->Available());

      STOUT_EVENTUALS_LOG(1)
        << "'" << wait_->scheduler_context_->name() << "' notified";

      wait_->notifiable_ = false;

      bool acquired = wait_->lock_->AcquireSlow(&wait_->waiter_);

      CHECK(!acquired) << "lock should be held when notifying";
    }
  }

  void Wait()
  {
    wait_->waited_ = true;
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
  using Value = typename ValueFrom<K_, Value_>::type;

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
      IsContinuation<K>::value, int> = 0>
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
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    return std::move(*this) | eventuals::Lambda(std::move(f));
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

    waited_ = false;
    notifiable_ = false;

    CHECK(!lock_->Available()) << "expecting lock to be acquired";

    if constexpr (IsUndefined<Context_>::value) {
      condition_(waitk_, std::forward<Args>(args)...);
    } else {
      condition_(context_, waitk_, std::forward<Args>(args)...);
    }

    if (waited_) {
      CHECK(!notifiable_) << "recursive wait detected (without notify)";

      // Mark we've waited in case we have recursive invocations of
      // 'Wait', e.g., when used with a stream/generator we don't want
      // to "re-wait" (and re-release the lock below) multiple times.
      waited_ = false;
      notifiable_ = true;

      static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                    "Wait only supports 0 or 1 argument, but found > 1");

      static_assert(IsUndefined<Arg_>::value || sizeof...(args) == 1);

      if constexpr (sizeof...(args) == 1) {
        CHECK(!arg_);
        arg_.emplace(std::forward<Args>(args)...);
      }

      scheduler_ = Scheduler::Get(&scheduler_context_);

      waiter_.f = [this]() mutable {
        STOUT_EVENTUALS_LOG(1)
          << "'" << scheduler_context_->name() << "' (notify) acquired";

        scheduler_->Submit(
            [this]() mutable {
              if constexpr (sizeof...(args) == 1) {
                Start(std::move(*arg_));
              } else {
                Start();
              }
            },
            scheduler_context_);

        STOUT_EVENTUALS_LOG(2)
          << "'" << scheduler_context_->name() << "' (notify) submitted";
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
  bool waited_ = false;
  bool notifiable_ = false;
  WaitK<Wait> waitk_;
  Scheduler* scheduler_ = nullptr;
  Scheduler::Context* scheduler_context_ = nullptr;
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
    auto k = eventuals::compose<Value>(std::move(acquire.k_));
    return detail::Acquire<decltype(k), Value>(std::move(k), acquire.lock_);
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
    auto k = eventuals::compose<Value>(std::move(release.k_));
    return detail::Release<decltype(k), Value>(std::move(k), release.lock_);
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
  auto Synchronized(E e) const
  {
    if constexpr (!IsContinuation<E>::value) {
      return Synchronized(Then(std::move(e)));
    } else {
      return Acquire(lock_)
        | std::move(e)
        | Release(lock_);
    }
  }

  template <typename T>
  auto Wait()
  {
    return eventuals::Wait<T>(lock_);
  }

private:
  Lock* lock_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {
