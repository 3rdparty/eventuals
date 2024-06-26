#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "eventuals/compose.h"
#include "eventuals/fork-join.h"
#include "eventuals/lazy.h"
#include "eventuals/scheduler.h"
#include "eventuals/semaphore.h"
#include "stout/borrowed_ptr.h"

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

  template <typename Iterable = std::vector<unsigned int>>
  static Pinned RandomCPU(
      const Iterable& cpu_mask = std::vector<unsigned int>()) {
    static std::random_device* device = new std::random_device();
    static std::mt19937* engine = new std::mt19937((*device)());
    static std::uniform_int_distribution<>* distribution =
        new std::uniform_int_distribution<>(
            0,
            std::thread::hardware_concurrency() - 1);

    unsigned int cpu = 0;
    do {
      cpu = (*distribution)(*engine);
    } while (std::find(
                 cpu_mask.begin(),
                 cpu_mask.end(),
                 cpu)
             != cpu_mask.end());

    CHECK_LT(cpu, std::thread::hardware_concurrency());
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

class StaticThreadPool final : public Scheduler {
 public:
  struct Requirements final {
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
      : requirements_(std::move(requirements)) {}

    Schedulable(Pinned pinned)
      : Schedulable(Requirements("[anonymous]", pinned)) {}

    virtual ~Schedulable() = default;

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

  ~StaticThreadPool() override;

  bool Continuable(const Context& context) override;

  void Submit(Callback<void()> callback, Context& context) override;

  void Clone(Context& child) override;

  template <typename E>
  [[nodiscard]] auto Schedule(Requirements* requirements, E e);

  template <typename E>
  [[nodiscard]] auto Schedule(
      std::string&& name,
      Requirements* requirements,
      E e);

  template <typename F, typename InsertableContainer>
  [[nodiscard]] auto ForkJoin(
      const std::string& name,
      unsigned int forks,
      InsertableContainer cpu_mask,
      F f);

  template <typename F>
  [[nodiscard]] auto ForkJoin(
      const std::string& name,
      unsigned int forks,
      F f) {
    return ForkJoin(name, forks, std::vector<unsigned int>(), std::move(f));
  }

  template <typename E>
  [[nodiscard]] static auto Spawn(Requirements&& requirements, E e);

 private:
  // NOTE: we use a semaphore instead of something like eventfd for
  // "signalling" the thread because it should be faster/less overhead
  // in the kernel: https://stackoverflow.com/q/9826919
  std::vector<Semaphore*> semaphores_;
  std::vector<std::atomic<Waiter*>*> heads_;
  std::deque<Semaphore> ready_;
  std::vector<std::thread> threads_;
  std::atomic<bool> shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

struct _StaticThreadPoolSchedule final {
  template <typename K_, typename E_, typename Arg_, typename Errors_>
  struct Continuation final
    : stout::enable_borrowable_from_this<Continuation<K_, E_, Arg_, Errors_>> {
    Continuation(
        K_ k,
        StaticThreadPool* pool,
        StaticThreadPool::Requirements* requirements,
        std::string&& name,
        E_ e)
      : e_(std::move(e)),
        context_(
            CHECK_NOTNULL(pool),
            std::move(name),
            CHECK_NOTNULL(requirements)),
        k_(std::move(k)) {}

    Continuation(Continuation&& that)
      : e_(std::move(that.e_)),
        context_(std::move(that.context_)),
        k_(std::move(that.k_)) {}

    ~Continuation() override {
      this->WaitUntilBorrowsEquals(0);
    }

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

      Pinned& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::RandomCPU();
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        context_->use();
        adapted_->Start(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
        context_->unuse();
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            this->Borrow([this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adapted_->Start(std::move(*arg_));
              } else {
                adapted_->Start();
              }
            }),
            *context_);
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      // NOTE: rather than skip the scheduling all together we make sure
      // to support the use case where code wants to "catch" a failure
      // inside of a 'Schedule()' in order to either recover or
      // propagate a different failure.

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      Pinned& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::RandomCPU();
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        context_->use();
        adapted_->Fail(std::forward<Error>(error));
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
        context_->unuse();
        ;
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Error>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Error>(error));

        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            this->Borrow([tuple = std::move(tuple)]() mutable {
              std::apply(
                  [](auto* schedule, auto&&... args) {
                    schedule->Adapt();
                    schedule->adapted_->Fail(
                        std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
            }),
            *context_);
      }
    }

    void Stop() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to "catch" the
      // stop inside of a 'Schedule()' in order to do something
      // different.

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      Pinned& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::RandomCPU();
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        context_->use();
        adapted_->Stop();
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
        context_->unuse();
      } else {
        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            this->Borrow([this]() {
              Adapt();
              adapted_->Stop();
            }),
            *context_);
      }
    }

    void Begin(TypeErasedStream& stream) {
      CHECK(stream_ == nullptr);
      stream_ = &stream;

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      Pinned& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::RandomCPU();
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        context_->use();
        adapted_->Begin(*CHECK_NOTNULL(stream_));
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
        context_->unuse();
      } else {
        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            this->Borrow([this]() {
              Adapt();
              adapted_->Begin(*CHECK_NOTNULL(stream_));
            }),
            *context_);
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      Pinned& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::RandomCPU();
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        context_->use();
        adapted_->Body(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
        context_->unuse();
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            this->Borrow([this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adapted_->Body(std::move(*arg_));
              } else {
                adapted_->Body();
              }
            }),
            *context_);
      }
    }

    void Ended() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to handle the
      // stream ended inside of a 'Schedule()' in order to do
      // something different.

      EVENTUALS_LOG(1) << "Scheduling '" << context_->name() << "'";

      Pinned& pinned = requirements()->pinned;

      if (!pinned.cpu()) {
        // TODO(benh): pick the least loaded cpu. This will require
        // iterating through and checking the sizes of all the "queues"
        // and then atomically incrementing which ever queue we pick
        // since we don't want to hold a lock here.
        pinned = Pinned::RandomCPU();
      }

      CHECK(pinned.cpu() <= pool()->concurrency);

      if (StaticThreadPool::member && StaticThreadPool::cpu == pinned.cpu()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        context_->use();
        adapted_->Ended();
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
        context_->unuse();
      } else {
        EVENTUALS_LOG(1)
            << "Schedule submitting '" << context_->name() << "'";

        pool()->Submit(
            this->Borrow([this]() {
              Adapt();
              adapted_->Ended();
            }),
            *context_);
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
        stout::borrowed_ref<Scheduler::Context> previous =
            Scheduler::Context::Get().reborrow();

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
                std::move(e_).template k<Arg_, Errors_>(
                    Reschedule(std::move(previous))
                        .template k<
                            Value_,
                            typename E_::template ErrorsFrom<
                                Arg_,
                                Errors_>>(std::move(k_)))));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }
    }

    E_ e_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    TypeErasedStream* stream_ = nullptr;

    Interrupt* interrupt_ = nullptr;

    // Need to store context using '_Lazy' because we need to be able to move
    // this class _before_ it's started and 'Context' is not movable.
    Lazy::Of<Scheduler::Context>::Args<
        StaticThreadPool*,
        std::string,
        StaticThreadPool::Requirements*>
        context_;

    using Value_ = typename E_::template ValueFrom<Arg_, Errors_>;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_, Errors_>(
        std::declval<_Reschedule::Composable>()
            .template k<
                Value_,
                typename E_::template ErrorsFrom<
                    Arg_,
                    Errors_>>(std::declval<K_>())));

    std::unique_ptr<Adapted_> adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename E_>
  struct Composable final {
    template <typename Arg, typename Errors>
    using ValueFrom = typename E_::template ValueFrom<Arg, Errors>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = typename E_::template ErrorsFrom<Arg, Errors>;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      return Continuation<K, E_, Arg, Errors>(
          std::move(k),
          pool_,
          requirements_,
          std::move(name_),
          std::move(e_));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = E_::template CanCompose<Downstream>;

    using Expects = StreamOrValue;

    StaticThreadPool* pool_ = nullptr;
    StaticThreadPool::Requirements* requirements_ = nullptr;
    E_ e_;
    std::string name_ = "[StaticThreadPool::Schedule - anonymous]";
  };
};

////////////////////////////////////////////////////////////////////////

template <typename E>
[[nodiscard]] auto StaticThreadPool::Schedule(Requirements* requirements, E e) {
  return _StaticThreadPoolSchedule::Composable<E>{
      this,
      requirements,
      std::move(e)};
}

template <typename E>
[[nodiscard]] auto StaticThreadPool::Schedule(
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
[[nodiscard]] auto StaticThreadPool::Schedulable::Schedule(E e) {
  return StaticThreadPool::Scheduler().Schedule(
      &requirements_,
      std::move(e));
}

template <typename E>
[[nodiscard]] auto StaticThreadPool::Schedulable::Schedule(
    std::string&& name,
    E e) {
  return StaticThreadPool::Scheduler().Schedule(
      std::move(name),
      &requirements_,
      std::move(e));
}

////////////////////////////////////////////////////////////////////////

template <typename F, typename InsertableContainer>
[[nodiscard]] auto StaticThreadPool::ForkJoin(
    const std::string& name,
    unsigned int forks,
    InsertableContainer cpu_mask,
    F f) {
  // TODO(benh): static_assert can insert an 'unsigned int' into 'cpu_mask'.

  static unsigned int hardware_concurrency =
      std::thread::hardware_concurrency();

  // TODO(benh): propagate errors instead of using 'CHECK'?
  for (unsigned int cpu : cpu_mask) {
    CHECK(cpu <= hardware_concurrency) << "CPU mask includes invalid CPUs";
  }

  CHECK(forks <= (hardware_concurrency - cpu_mask.size()))
      << "Insufficient hardware concurrency (" << hardware_concurrency
      << ") given CPU mask (" << cpu_mask.size()
      << ") and forks (" << forks << ")";

  return eventuals::ForkJoin(
      "StaticThreadPool - " + name,
      forks,
      [this, name, f = std::move(f), cpu_mask = std::move(cpu_mask)](
          size_t index,
          auto&&... arg) mutable {
        Pinned pinned = Pinned::RandomCPU(cpu_mask);

        // Add selected CPU to our mask so we won't use again.
        CHECK(pinned.cpu().has_value());
        cpu_mask.insert(cpu_mask.end(), pinned.cpu().value());

        Requirements requirements(
            name + "/" + std::to_string(index) + " Requirements",
            std::move(pinned));

        auto e = f(index, std::forward<decltype(arg)>(arg)...);

        return Closure(
            [this,
             requirements = std::move(requirements),
             e = std::move(e)]() mutable {
              return Schedule(&requirements, std::move(e));
            });
      });
}

////////////////////////////////////////////////////////////////////////

template <typename E>
[[nodiscard]] auto StaticThreadPool::Spawn(Requirements&& requirements, E e) {
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
