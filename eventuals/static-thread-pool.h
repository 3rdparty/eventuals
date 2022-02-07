#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "eventuals/lazy.h"
#include "eventuals/scheduler.h"
#include "eventuals/semaphore.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct Pinned {
  static Pinned ModuloTotalCPUs(unsigned int cpu) {
    return Pinned(cpu % std::thread::hardware_concurrency());
  }

  static Pinned Any() {
    return Pinned();
  }

  static Pinned ExactCPU(unsigned int cpu) {
    CHECK(cpu < std::thread::hardware_concurrency())
        << "specified CPU is not valid";
    return Pinned(cpu);
  }

  std::optional<unsigned int> cpu() {
    return cpu_;
  }

  Pinned(const Pinned& that)
    : cpu_(that.cpu_) {}

 private:
  Pinned() = default;

  Pinned(unsigned int cpu)
    : cpu_(cpu) {}

  std::optional<unsigned int> cpu_;
};

////////////////////////////////////////////////////////////////////////

class StaticThreadPool : public Scheduler {
 public:
  struct Requirements {
    Requirements(const char* name, Pinned pinned = Pinned::Any())
      : Requirements(std::string(name), std::move(pinned)) {}

    Requirements(std::string name, Pinned pinned = Pinned::Any())
      : name(std::move(name)),
        pinned(pinned) {}

    std::string name;
    Pinned pinned;
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

    template <typename E>
    auto Schedule(std::string&& name, E e);

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

  // If 'member', for which cpu?
  static inline thread_local unsigned int cpu = 0;

  const unsigned int concurrency;

  StaticThreadPool();

  ~StaticThreadPool();

  bool Continuable(Context* context) override;

  void Submit(Callback<> callback, Context* context) override;

  void Clone(Context* child) override;

  template <typename E>
  auto Schedule(Requirements* requirements, E e);

  template <typename E>
  auto Schedule(std::string&& name, Requirements* requirements, E e);

  template <typename E>
  static auto Spawn(Requirements&& requirements, E e);

 private:
  // NOTE: we use a semaphore instead of something like eventfd for
  // "signalling" the thread because it should be faster/less overhead
  // in the kernel: https://stackoverflow.com/q/9826919
  std::vector<Semaphore*> semaphores_;
  std::vector<std::atomic<Context*>*> heads_;
  std::deque<Semaphore> ready_;
  std::vector<std::thread> threads_;
  std::atomic<bool> shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

struct _StaticThreadPoolSchedule {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation {
    Continuation(
        K_ k,
        StaticThreadPool* pool,
        StaticThreadPool::Requirements* requirements,
        std::string&& name,
        E_ e)
      : k_(std::move(k)),
        e_(std::move(e)),
        context_(
            std::make_tuple(
                CHECK_NOTNULL(pool),
                std::move(name),
                CHECK_NOTNULL(requirements))) {}

    // Helper to avoid casting default 'Scheduler*' to 'StaticThreadPool*'
    // each time.
    auto* pool() {
      return static_cast<StaticThreadPool*>(context_->scheduler());
    }

    // Helper to avoid casting 'void*' to 'StaticThreadPool::Requirements*'
    // each time.
    auto* requirements() {
      return static_cast<StaticThreadPool::Requirements*>(context_->data);
    }

    template <typename... Args>
    void Start(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");


      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::ExactCPU(0);
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Start(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            [this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adapted_->Start(std::move(*arg_));
              } else {
                adapted_->Start();
              }
            },
            context_.get());
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // NOTE: rather than skip the scheduling all together we make sure
      // to support the use case where code wants to "catch" a failure
      // inside of a 'Schedule()' in order to either recover or
      // propagate a different failure.

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::ExactCPU(0);
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Fail(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Args...>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Args>(args)...);

        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

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
            context_.get());
      }
    }

    void Stop() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to "catch" the
      // stop inside of a 'Schedule()' in order to do something
      // different.

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::ExactCPU(0);
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Stop();
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            [this]() {
              Adapt();
              adapted_->Stop();
            },
            context_.get());
      }
    }

    void Begin(TypeErasedStream& stream) {
      CHECK(stream_ == nullptr);
      stream_ = &stream;

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::ExactCPU(0);
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Begin(*CHECK_NOTNULL(stream_));
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            [this]() {
              Adapt();
              adapted_->Begin(*CHECK_NOTNULL(stream_));
            },
            context_.get());
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::ExactCPU(0);
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Body(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            [this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adapted_->Body(std::move(*arg_));
              } else {
                adapted_->Body();
              }
            },
            context_.get());
      }
    }

    void Ended() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to handle the
      // stream ended inside of a 'Schedule()' in order to do
      // something different.

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      auto& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::ExactCPU(0);
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Ended();
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            [this]() {
              Adapt();
              adapted_->Ended();
            },
            context_.get());
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

    // Need to store context using '_Lazy' because we need to be able to move
    // this class _before_ it's started and 'Context' is not movable.
    _Lazy<
        Scheduler::Context,
        StaticThreadPool*,
        std::string,
        StaticThreadPool::Requirements*>
        context_;

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
          std::move(name_),
          std::move(e_));
    }

    StaticThreadPool* pool_;
    StaticThreadPool::Requirements* requirements_;
    E_ e_;
    std::string name_;
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

template <typename E>
auto StaticThreadPool::Schedule(
    std::string&& name,
    Requirements* requirements,
    E e) {
  return _StaticThreadPoolSchedule::Composable<E>{
      this,
      requirements,
      std::move(e),
      std::move(name)};
}

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedulable::Schedule(E e) {
  return StaticThreadPool::Scheduler().Schedule(
      &requirements_,
      std::move(e));
}

template <typename E>
auto StaticThreadPool::Schedulable::Schedule(std::string&& name, E e) {
  return StaticThreadPool::Scheduler().Schedule(
      std::move(name),
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
