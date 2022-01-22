#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

#include "eventuals/scheduler.h"
#include "eventuals/semaphore.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct Pinned {
  Pinned() {}

  Pinned(unsigned int core)
    : core(core) {}

  Pinned(const Pinned& that)
    : core(that.core) {}

  std::optional<unsigned int> core;
};

////////////////////////////////////////////////////////////////////////

class StaticThreadPool : public Scheduler {
 public:
  struct Requirements {
    Requirements(const char* name)
      : Requirements(std::string(name)) {}

    Requirements(const char* name, Pinned pinned)
      : Requirements(std::string(name), std::move(pinned)) {}

    Requirements(std::string name)
      : name(std::move(name)) {}

    Requirements(std::string name, Pinned pinned)
      : name(std::move(name)),
        pinned(pinned) {}

    std::string name;
    Pinned pinned;
  };

  struct Waiter : public Scheduler::Context {
   public:
    Waiter(StaticThreadPool* pool, Requirements* requirements)
      : Scheduler::Context(pool),
        requirements_(requirements) {}

    Waiter(Waiter&& that)
      : Scheduler::Context(that.scheduler()),
        requirements_(that.requirements_) {
      // NOTE: should only get moved before it's "started".
      CHECK(!that.waiting && !callback && next == nullptr);
    }

    const std::string& name() override {
      return requirements_->name;
    }

    StaticThreadPool* pool() {
      return static_cast<StaticThreadPool*>(scheduler());
    }

    auto* requirements() {
      return requirements_;
    }

    bool waiting = false;
    Callback<> callback;
    Waiter* next = nullptr;

   private:
    Requirements* requirements_;
  };

  class Schedulable {
   public:
    Schedulable(Requirements requirements = Requirements("[anonymous]"))
      : requirements_(requirements) {}

    Schedulable(Pinned pinned)
      : Schedulable(Requirements("[anonymous]", pinned)) {}

    virtual ~Schedulable() {}

    template <typename E>
    auto Schedule(E e);

    auto* requirements() {
      return &requirements_;
    }

   private:
    Requirements requirements_;
  };

  static StaticThreadPool& Scheduler() {
    static StaticThreadPool pool;
    return pool;
  }

  // Is thread a member of the static pool?
  static inline thread_local bool member = false;

  // If 'member', for which core?
  static inline thread_local unsigned int core = 0;

  const unsigned int concurrency;

  StaticThreadPool();

  ~StaticThreadPool();

  bool Continuable(Context* context) override;

  void Submit(Callback<> callback, Context* context) override;

  template <typename E>
  auto Schedule(Requirements* requirements, E e);

  template <typename E>
  static auto Spawn(Requirements&& requirements, E e);

 private:
  // NOTE: we use a semaphore instead of something like eventfd for
  // "signalling" the thread because it should be faster/less overhead
  // in the kernel: https://stackoverflow.com/q/9826919
  std::vector<Semaphore*> semaphores_;
  std::vector<std::atomic<Waiter*>*> heads_;
  std::deque<Semaphore> ready_;
  std::vector<std::thread> threads_;
  std::atomic<bool> shutdown_ = false;
  size_t next_ = 0;
};

////////////////////////////////////////////////////////////////////////

struct _StaticThreadPoolSchedule {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation : public StaticThreadPool::Waiter {
    // NOTE: explicit constructor because inheriting 'StaticThreadPool::Waiter'.
    Continuation(
        K_ k,
        StaticThreadPool* pool,
        StaticThreadPool::Requirements* requirements,
        E_ e)
      : StaticThreadPool::Waiter(pool, requirements),
        k_(std::move(k)),
        e_(std::move(e)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");

      EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.core) {
        // TODO(benh): pick the least loaded core. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned.core = 0;
      }

      assert(pinned.core);

      if (!(*pinned.core < pool()->concurrency)) {
        CHECK(!adapted_);
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }
        k_.Fail("'" + name() + "' required core is > total cores");
      } else {
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          adapted_->Start(std::forward<Args>(args)...);
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          if constexpr (!std::is_void_v<Arg_>) {
            arg_.emplace(std::forward<Args>(args)...);
          }

          EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                if constexpr (sizeof...(args) > 0) {
                  adapted_->Start(std::move(*arg_));
                } else {
                  adapted_->Start();
                }
              },
              this);
        }
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // NOTE: rather than skip the scheduling all together we make sure
      // to support the use case where code wants to "catch" a failure
      // inside of a 'Schedule()' in order to either recover or
      // propagate a different failure.
      EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.core) {
        // TODO(benh): pick the least loaded core. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned.core = 0;
      }

      assert(pinned.core);

      if (!(*pinned.core < pool()->concurrency)) {
        CHECK(!adapted_);
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }
        k_.Fail("'" + name() + "' required core is > total cores");
      } else {
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          adapted_->Fail(std::forward<Args>(args)...);
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          // TODO(benh): avoid allocating on heap by storing args in
          // pre-allocated buffer based on composing with Errors.
          using Tuple = std::tuple<decltype(this), Args...>;
          auto tuple = std::make_unique<Tuple>(
              this,
              std::forward<Args>(args)...);

          EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [tuple = std::move(tuple)]() mutable {
                std::apply(
                    [](auto* schedule, auto&&... args) {
                      schedule->Adapt();
                      schedule->adapted_->Fail(
                          std::forward<decltype(args)>(args)...);
                    },
                    std::move(*tuple));
              },
              this);
        }
      }
    }

    void Stop() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to "catch" the
      // stop inside of a 'Schedule()' in order to do something
      // different.
      EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.core) {
        // TODO(benh): pick the least loaded core. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned.core = 0;
      }

      assert(pinned.core);

      if (!(*pinned.core < pool()->concurrency)) {
        CHECK(!adapted_);
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }
        k_.Fail("'" + name() + "' required core is > total cores");
      } else {
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          adapted_->Stop();
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                adapted_->Stop();
              },
              this);
        }
      }
    }

    void Begin(TypeErasedStream& stream) {
      CHECK(stream_ == nullptr);
      stream_ = &stream;

      EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.core) {
        // TODO(benh): pick the least loaded core. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned.core = 0;
      }

      assert(pinned.core);

      if (!(*pinned.core < pool()->concurrency)) {
        CHECK(!adapted_);
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }
        k_.Fail("'" + name() + "' required core is > total cores");
      } else {
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          adapted_->Begin(*CHECK_NOTNULL(stream_));
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                adapted_->Begin(*CHECK_NOTNULL(stream_));
              },
              this);
        }
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");

      EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.core) {
        // TODO(benh): pick the least loaded core. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned.core = 0;
      }

      assert(pinned.core);

      if (!(*pinned.core < pool()->concurrency)) {
        CHECK(!adapted_);
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }
        k_.Fail("'" + name() + "' required core is > total cores");
      } else {
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          adapted_->Body(std::forward<Args>(args)...);
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          if constexpr (!std::is_void_v<Arg_>) {
            arg_.emplace(std::forward<Args>(args)...);
          }

          EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                if constexpr (sizeof...(args) > 0) {
                  adapted_->Body(std::move(*arg_));
                } else {
                  adapted_->Body();
                }
              },
              this);
        }
      }
    }

    void Ended() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to handle the
      // stream ended inside of a 'Schedule()' in order to do
      // something different.
      EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.core) {
        // TODO(benh): pick the least loaded core. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned.core = 0;
      }

      assert(pinned.core);

      if (!(*pinned.core < pool()->concurrency)) {
        CHECK(!adapted_);
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }
        k_.Fail("'" + name() + "' required core is > total cores");
      } else {
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          adapted_->Ended();
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                adapted_->Ended();
              },
              this);
        }
      }
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;

      // NOTE: we propagate interrupt registration when we adapt or
      // when we call 'Fail()' in the cases when we aren't adapting.
    }

    void Adapt() {
      if (!adapted_) {
        // Save previous context (even if it's us).
        Scheduler::Context* previous = Scheduler::Context::Get();

        adapted_.reset(
            // NOTE: for now we're assuming usage of something like
            // 'jemalloc' so 'new' should use lock-free and thread-local
            // arenas. Ideally allocating memory during runtime should
            // actually be *faster* because the memory should have
            // better locality for the execution resource being used
            // (i.e., a local NUMA node). However, we should reconsider
            // this design decision if in practice this performance
            // tradeoff is not emperically a benefit.
            new Adapted_(
                std::move(e_).template k<Arg_>(
                    Reschedule(previous).template k<Value_>(
                        std::move(k_)))));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }
    }

    K_ k_;
    E_ e_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    TypeErasedStream* stream_ = nullptr;

    Interrupt* interrupt_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<_Reschedule::Composable>()
            .template k<Value_>(std::declval<K_>())));

    std::unique_ptr<Adapted_> adapted_;
  };

  template <typename E_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, E_, Arg>(
          std::move(k),
          pool_,
          requirements_,
          std::move(e_));
    }

    StaticThreadPool* pool_;
    StaticThreadPool::Requirements* requirements_;
    E_ e_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedule(Requirements* requirements, E e) {
  return _StaticThreadPoolSchedule::Composable<E>{
      this,
      requirements,
      std::move(e)};
}

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedulable::Schedule(E e) {
  return StaticThreadPool::Scheduler().Schedule(
      &requirements_,
      std::move(e));
}

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Spawn(Requirements&& requirements, E e) {
  return Closure([requirements = std::move(requirements),
                  e = std::move(e)]() mutable {
    return StaticThreadPool::Scheduler().Schedule(
        &requirements,
        std::move(e));
  });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
