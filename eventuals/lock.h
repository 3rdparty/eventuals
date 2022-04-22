#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include "eventuals/callback.h"
#include "eventuals/eventual.h"
#include "eventuals/scheduler.h"
#include "eventuals/stream.h"
#include "eventuals/then.h"
#include "eventuals/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Lock final {
 public:
  struct Waiter final {
    Callback<void()> f;
    Waiter* next = nullptr;
    bool acquired = false;
    Scheduler::Context* context = nullptr;
  };

  bool AcquireFast(Waiter* waiter) {
    CHECK(!waiter->acquired) << "recursive lock acquire detected";
    CHECK(waiter->next == nullptr);

    waiter->next = head_.load(std::memory_order_relaxed);

    while (waiter->next == nullptr) {
      if (head_.compare_exchange_weak(
              waiter->next,
              waiter,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        owner_.store(CHECK_NOTNULL(waiter->context));
        waiter->acquired = true;
        return true;
      }
    }

    waiter->next = nullptr;

    return false;
  }

  bool AcquireSlow(Waiter* waiter) {
    CHECK(!waiter->acquired) << "recursive lock acquire detected";
    CHECK(waiter->next == nullptr);

  load:
    waiter->next = head_.load(std::memory_order_relaxed);
  loop:
    if (waiter->next == nullptr) {
      if (AcquireFast(waiter)) {
        return true;
      } else {
        goto load;
      }
    } else {
      if (head_.compare_exchange_weak(
              waiter->next,
              waiter,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        return false;
      } else {
        goto loop;
      }
    }
  }

  void Release() {
    EVENTUALS_LOG(2)
        << "'" << Scheduler::Context::Get()->name() << "' releasing";

    auto* waiter = head_.load(std::memory_order_relaxed);

    // Should have at least one waiter (who ever acquired) even if
    // they're aren't any others waiting.
    CHECK_NOTNULL(waiter);

    if (waiter->next == nullptr) {
      // Unset owner _now_ instead of _after_ the "compare and swap"
      // to avoid racing with 'AcquireFast()' trying to set the owner.
      owner_.store(nullptr);

      if (!head_.compare_exchange_weak(
              waiter,
              nullptr,
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
      waiter->next = nullptr;

      owner_.store(CHECK_NOTNULL(waiter->context));

      waiter->acquired = true;
      waiter->f();
    }
  }

  bool Available() {
    return head_.load(std::memory_order_relaxed) == nullptr;
  }

  bool OwnedByCurrentSchedulerContext() {
    // NOTE: using 'CHECK_NOTNULL' because the intention here is that
    // the caller expects to have a current scheduler context.
    return owner_.load() == CHECK_NOTNULL(Scheduler::Context::Get());
  }

 private:
  std::atomic<Waiter*> head_ = nullptr;

  // NOTE: we store the owning scheduler context pointer in 'owner_'
  // rather than using 'head_' to lookup the context because of the
  // possibility that the lookup will end up dereferencing a 'Waiter'
  // that has since been deleted leading to undefined
  // behavior. Instead, it's possible that 'owner_' may be out of date
  // or a 'nullptr' but it will never read deallocated memory.
  std::atomic<Scheduler::Context*> owner_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

struct _Acquire final {
  template <typename K_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, Lock* lock)
      : lock_(lock),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      waiter_.context = Scheduler::Context::Get();

      EVENTUALS_LOG(2)
          << "'" << waiter_.context->name() << "' acquiring";

      if (lock_->AcquireFast(&waiter_)) {
        EVENTUALS_LOG(2)
            << "'" << waiter_.context->name() << "' (fast) acquired";
        k_.Start(std::forward<Args>(args)...);
      } else {
        static_assert(
            sizeof...(args) == 0 || sizeof...(args) == 1,
            "Acquire only supports 0 or 1 argument, but found > 1");

        static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        waiter_.f = [this]() mutable {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (very slow) acquired";

          waiter_.context->Unblock([this]() mutable {
            if constexpr (sizeof...(args) == 1) {
              k_.Start(std::move(*arg_));
            } else {
              k_.Start();
            }
          });
        };

        if (lock_->AcquireSlow(&waiter_)) {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (slow) acquired";

          if constexpr (sizeof...(args) == 1) {
            k_.Start(std::move(*arg_));
          } else {
            k_.Start();
          }
        }
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      waiter_.context = Scheduler::Context::Get();

      if (lock_->AcquireFast(&waiter_)) {
        k_.Fail(std::move(error));
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Error>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Error>(error));

        waiter_.f = [tuple = std::move(tuple)]() mutable {
          auto* acquire = std::get<0>(*tuple);
          acquire->waiter_.context->Unblock(
              [tuple = std::move(tuple)]() mutable {
                std::apply(
                    [](auto* acquire, auto&&... args) {
                      auto& k_ = acquire->k_;
                      k_.Fail(std::forward<decltype(args)>(args)...);
                    },
                    std::move(*tuple));
              });
        };

        if (lock_->AcquireSlow(&waiter_)) {
          // TODO(benh): while this isn't the "fast path" we'll do a
          // Context::Unblock() here which will make it an even slower
          // path because we'll defer continued execution rather than
          // execute immediately unless we enhance 'Unblock()' to be
          // able to tell it to "execute immediately if possible".
          waiter_.f();
        }
      }
    }

    void Stop() {
      waiter_.context = Scheduler::Context::Get();

      if (lock_->AcquireFast(&waiter_)) {
        k_.Stop();
      } else {
        waiter_.f = [this]() mutable {
          waiter_.context->Unblock([this]() mutable {
            k_.Stop();
          });
        };

        if (lock_->AcquireSlow(&waiter_)) {
          // TODO(benh): while this isn't the "fast path" we'll do a
          // Context::Unblock() here which will make it an even slower
          // path because we'll defer continued execution rather than
          // execute immediately unless we enhance 'Unblock()' to be
          // able to tell it to "execute immediately if possible".
          waiter_.f();
        }
      }
    }

    void Begin(TypeErasedStream& stream) {
      waiter_.context = Scheduler::Context::Get();

      CHECK(stream_ == nullptr);
      stream_ = &stream;

      EVENTUALS_LOG(2)
          << "'" << waiter_.context->name() << "' acquiring";

      if (lock_->AcquireFast(&waiter_)) {
        EVENTUALS_LOG(2)
            << "'" << waiter_.context->name() << "' (fast) acquired";
        k_.Begin(*CHECK_NOTNULL(stream_));
      } else {
        waiter_.f = [this]() mutable {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (very slow) acquired";

          waiter_.context->Unblock([this]() mutable {
            k_.Begin(*CHECK_NOTNULL(stream_));
          });
        };

        if (lock_->AcquireSlow(&waiter_)) {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (slow) acquired";

          k_.Begin(*CHECK_NOTNULL(stream_));
        }
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      waiter_.context = Scheduler::Context::Get();

      EVENTUALS_LOG(2)
          << "'" << waiter_.context->name() << "' acquiring";

      if (lock_->AcquireFast(&waiter_)) {
        EVENTUALS_LOG(2)
            << "'" << waiter_.context->name() << "' (fast) acquired";
        k_.Body(std::forward<Args>(args)...);
      } else {
        static_assert(
            sizeof...(args) == 0 || sizeof...(args) == 1,
            "Acquire only supports 0 or 1 argument, but found > 1");

        static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        waiter_.f = [this]() mutable {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (very slow) acquired";

          waiter_.context->Unblock([this]() mutable {
            if constexpr (sizeof...(args) == 1) {
              k_.Body(std::move(*arg_));
            } else {
              k_.Body();
            }
          });
        };

        if (lock_->AcquireSlow(&waiter_)) {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (slow) acquired";

          if constexpr (sizeof...(args) == 1) {
            k_.Body(std::move(*arg_));
          } else {
            k_.Body();
          }
        }
      }
    }

    void Ended() {
      waiter_.context = Scheduler::Context::Get();

      if (lock_->AcquireFast(&waiter_)) {
        k_.Ended();
      } else {
        waiter_.f = [this]() mutable {
          waiter_.context->Unblock([this]() mutable {
            k_.Ended();
          });
        };

        if (lock_->AcquireSlow(&waiter_)) {
          // TODO(benh): while this isn't the "fast path" we'll do a
          // Context::Unblock() here which will make it an even slower
          // path because we'll defer continued execution rather than
          // execute immediately unless we enhance 'Unblock()' to be
          // able to tell it to "execute immediately if possible".
          waiter_.f();
        }
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    Lock* lock_;
    Lock::Waiter waiter_;
    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;
    TypeErasedStream* stream_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>(std::move(k), lock_);
    }

    Lock* lock_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _Release final {
  template <typename K_>
  struct Continuation final {
    Continuation(K_ k, Lock* lock)
      : lock_(lock),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Start(std::forward<decltype(args)>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Stop();
    }

    void Begin(TypeErasedStream& stream) {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Begin(stream);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Body(std::forward<decltype(args)>(args)...);
    }

    void Ended() {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    Lock* lock_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K>(std::move(k), lock_);
    }

    Lock* lock_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _Wait final {
  template <typename K_, typename F_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, Lock* lock, F_ f)
      : lock_(lock),
        f_(std::move(f)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      CHECK(!lock_->Available()) << "expecting lock to be acquired";

      notifiable_ = false;

      if (!condition_) {
        condition_.emplace(
            f_(Callback<void()>([this]() {
              // NOTE: we ignore notifications unless we're notifiable
              // and we make sure we're not notifiable after the first
              // notification so we don't try and add ourselves to the
              // list of waiters again.
              if (notifiable_) {
                CHECK(lock_->OwnedByCurrentSchedulerContext());

                EVENTUALS_LOG(2)
                    << "'" << CHECK_NOTNULL(waiter_.context)->name()
                    << "' notified";

                notifiable_ = false;

                bool acquired = lock_->AcquireSlow(&waiter_);

                CHECK(!acquired) << "lock should be held when notifying";
              }
            })));
      }

      if ((*condition_)(std::forward<Args>(args)...)) {
        CHECK(!notifiable_) << "recursive wait detected (without notify)";

        notifiable_ = true;

        static_assert(
            sizeof...(args) == 0 || sizeof...(args) == 1,
            "Wait only supports 0 or 1 argument, but found > 1");

        static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        waiter_.context = Scheduler::Context::Get();

        waiter_.f = [this]() mutable {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (notify) acquired";

          waiter_.context->Unblock([this]() mutable {
            if constexpr (sizeof...(args) == 1) {
              Start(std::move(*arg_));
            } else {
              Start();
            }
          });

          EVENTUALS_LOG(2)
              << "'" << CHECK_NOTNULL(waiter_.context)->name()
              << "' (notify) submitted";
        };

        lock_->Release();
      } else {
        k_.Start(std::forward<Args>(args)...);
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      CHECK(!lock_->Available()) << "expecting lock to be acquired";

      notifiable_ = false;

      if (!condition_) {
        condition_.emplace(
            f_(Callback<void()>([this]() {
              // NOTE: we ignore notifications unless we're notifiable
              // and we make sure we're not notifiable after the first
              // notification so we don't try and add ourselves to the
              // list of waiters again.
              //
              // TODO(benh): make sure *we've* acquired the lock
              // (where 'we' is the current "eventual").
              if (notifiable_) {
                CHECK(!lock_->Available());

                EVENTUALS_LOG(2)
                    << "'" << CHECK_NOTNULL(waiter_.context)->name()
                    << "' notified";

                notifiable_ = false;

                bool acquired = lock_->AcquireSlow(&waiter_);

                CHECK(!acquired) << "lock should be held when notifying";
              }
            })));
      }

      if ((*condition_)(std::forward<Args>(args)...)) {
        CHECK(!notifiable_) << "recursive wait detected (without notify)";

        notifiable_ = true;

        static_assert(
            sizeof...(args) == 0 || sizeof...(args) == 1,
            "Wait only supports 0 or 1 argument, but found > 1");

        static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        waiter_.context = Scheduler::Context::Get();

        waiter_.f = [this]() mutable {
          EVENTUALS_LOG(2)
              << "'" << waiter_.context->name() << "' (notify) acquired";

          waiter_.context->Unblock([this]() mutable {
            if constexpr (sizeof...(args) == 1) {
              Body(std::move(*arg_));
            } else {
              Body();
            }
          });

          EVENTUALS_LOG(2)
              << "'" << CHECK_NOTNULL(waiter_.context)->name()
              << "' (notify) submitted";
        };

        lock_->Release();
      } else {
        k_.Body(std::forward<Args>(args)...);
      }
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    Lock* lock_;
    F_ f_;

    std::optional<decltype(f_(std::declval<Callback<void()>>()))> condition_;
    Lock::Waiter waiter_;
    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;
    bool notifiable_ = false;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>(std::move(k), lock_, std::move(f_));
    }

    Lock* lock_;
    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Acquire(Lock* lock) {
  return _Acquire::Composable{lock};
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Release(Lock* lock) {
  return _Release::Composable{lock};
}

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Wait(Lock* lock, F f) {
  return _Wait::Composable<F>{lock, std::move(f)};
}

////////////////////////////////////////////////////////////////////////

class Synchronizable {
 public:
  virtual ~Synchronizable() = default;

  template <typename E>
  [[nodiscard]] auto Synchronized(E e) {
    return Acquire(&lock_)
        | std::move(e)
        | Release(&lock_);
  }

  template <typename F>
  [[nodiscard]] auto Wait(F f) {
    return eventuals::Wait(&lock_, std::move(f));
  }

  Lock& lock() {
    return lock_;
  }

 private:
  Lock lock_;
};

////////////////////////////////////////////////////////////////////////

class ConditionVariable final {
 public:
  ConditionVariable(Lock* lock)
    : lock_(CHECK_NOTNULL(lock)) {}

  template <typename F>
  [[nodiscard]] auto Wait(F f) {
    return eventuals::Wait(
        lock_,
        [this, f = std::move(f), waiter = Waiter()](auto notify) mutable {
          // Check if template `F` is for `EmptyCondition`.
          constexpr bool using_empty_condition =
              std::is_same_v<F, EmptyCondition>;

          // Helper to determine if we need to wait or not.
          auto should_wait = [&]() {
            if constexpr (using_empty_condition) {
              return f(waiter);
            } else {
              return f();
            }
          };

          // If we should wait, the `waiter` need to be
          // enqueued with other waiters so it can later be notified.
          if (should_wait()) {
            // Assign `nofify` callback to `waiter` for later use.
            waiter.notify = std::move(notify);

            // Add `waiter` to list of waiters. The below might look convoluted
            // at first but is a text book "append" to a linked list.
            if (head_ == nullptr) {
              head_ = &waiter;
            } else if (head_->next == nullptr) {
              head_->next = &waiter;
            } else {
              auto* next = head_->next;
              while (next->next != nullptr) {
                next = next->next;
              }
              next->next = &waiter;
            }
          }
          return should_wait;
        });
  }

  [[nodiscard]] auto Wait() {
    return Wait(EmptyCondition());
  }

  Lock* lock() {
    return lock_;
  }

  void Notify() {
    CHECK(lock_->OwnedByCurrentSchedulerContext());
    Waiter* waiter = head_;
    if (waiter != nullptr) {
      head_ = waiter->next;

      waiter->next = nullptr;
      waiter->notified = true;
      waiter->notify();
    }
  }

  void NotifyAll() {
    CHECK(lock_->OwnedByCurrentSchedulerContext());
    while (head_ != nullptr) {
      Notify();
    }
  }

 private:
  Lock* lock_ = nullptr;

  // Using an _intrusive_ linked list here so we don't have to do any
  // dynamic memory allocation for each waiter. Instead the allocation
  // takes place "on the stack" as part of the 'Wait()' above. This is
  // safe because it must be the case that 'Notify()' has been called
  // in order for 'Wait()' to return and thus there are no races with
  // accessing any of the linked list 'next' pointers.
  struct Waiter {
    Callback<void()> notify;
    bool notified = false;
    Waiter* next = nullptr;
  };

  // Head of the intrusive linked list of waiters.
  Waiter* head_ = nullptr;

  // Helper struct for when no condition function is specified.
  struct EmptyCondition {
    bool operator()(const Waiter& waiter) const {
      return !waiter.notified;
    }
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
