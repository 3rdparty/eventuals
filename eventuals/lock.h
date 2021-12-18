#pragma once

#include <atomic>
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

class Lock {
 public:
  struct Waiter {
    Callback<> f;
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

struct _Acquire {
  template <typename K_, typename Arg_>
  struct Continuation {
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

    template <typename... Args>
    void Fail(Args&&... args) {
      waiter_.context = Scheduler::Context::Get();

      if (lock_->AcquireFast(&waiter_)) {
        k_.Fail(std::forward<Args>(args)...);
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        auto* tuple = new std::tuple{this, std::forward<Args>(args)...};

        waiter_.f = [tuple]() mutable {
          std::apply(
              [tuple](auto* acquire, auto&&...) {
                acquire->waiter_.context->Unblock([tuple]() mutable {
                  std::apply(
                      [](auto* acquire, auto&&... args) {
                        auto& k_ = acquire->k_;
                        k_.Fail(std::forward<decltype(args)>(args)...);
                      },
                      std::move(*tuple));
                  delete tuple;
                });
              },
              std::move(*tuple));
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

    K_ k_;
    Lock* lock_;
    Lock::Waiter waiter_;
    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;
    TypeErasedStream* stream_ = nullptr;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k), lock_};
    }

    Lock* lock_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _Release {
  template <typename K_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Start(std::forward<decltype(args)>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      CHECK(!lock_->Available());
      lock_->Release();
      k_.Fail(std::forward<Args>(args)...);
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

    K_ k_;
    Lock* lock_;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K>{std::move(k), lock_};
    }

    Lock* lock_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _Wait {
  template <typename K_, typename F_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      CHECK(!lock_->Available()) << "expecting lock to be acquired";

      notifiable_ = false;

      if (!condition_) {
        condition_.emplace(
            f_(Callback<>([this]() {
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

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
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
            f_(Callback<>([this]() {
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

    K_ k_;
    Lock* lock_;
    F_ f_;

    std::optional<decltype(f_(std::declval<Callback<>>()))> condition_;
    Lock::Waiter waiter_;
    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;
    bool notifiable_ = false;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>{std::move(k), lock_, std::move(f_)};
    }

    Lock* lock_;
    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

inline auto Acquire(Lock* lock) {
  return _Acquire::Composable{lock};
}

////////////////////////////////////////////////////////////////////////

inline auto Release(Lock* lock) {
  return _Release::Composable{lock};
}

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Wait(Lock* lock, F f) {
  return _Wait::Composable<F>{lock, std::move(f)};
}

////////////////////////////////////////////////////////////////////////

class Synchronizable {
 public:
  virtual ~Synchronizable() {}

  template <typename E>
  auto Synchronized(E e) {
    return Acquire(&lock_)
        | std::move(e)
        | Release(&lock_);
  }

  template <typename F>
  auto Wait(F f) {
    return eventuals::Wait(&lock_, std::move(f));
  }

  Lock& lock() {
    return lock_;
  }

 private:
  Lock lock_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
