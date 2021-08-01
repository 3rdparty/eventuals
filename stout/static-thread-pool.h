#pragma once

#include <atomic>
#include <deque>
#include <thread>
#include <vector>

#include "stout/closure.h"
#include "stout/just.h"
#include "stout/lambda.h"
#include "stout/lock.h"
#include "stout/reduce.h"
#include "stout/repeat.h"
#include "stout/scheduler.h"
#include "stout/semaphore.h"
#include "stout/task.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "stout/until.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct Pinned
{
  Pinned() {}
  Pinned(unsigned int core) : core(core) {}
  Pinned(const Pinned& that) : core(that.core) {}

  std::optional<unsigned int> core;
};

////////////////////////////////////////////////////////////////////////

class StaticThreadPool : public Scheduler
{
public:
  struct Requirements
  {
    Requirements() {}
    Requirements(Pinned pinned) : pinned(pinned) {}

    Pinned pinned;

    bool preempt = false;

    std::string name;
  };

  struct Waiter : public Scheduler::Context
  {
  public:
    Waiter(StaticThreadPool* pool, Requirements* requirements)
      : Scheduler::Context { &requirements->name },
        pool_(pool),
        requirements_(requirements) {}

    auto* pool() { return pool_; }
    auto* requirements() { return requirements_; }

    bool waiting = false;
    Callback<> callback;
    Waiter* next = nullptr;

  private:
    StaticThreadPool* pool_;
    Requirements* requirements_;
  };

  class Schedulable
  {
  public:
    Schedulable(Requirements requirements = Requirements())
      : requirements_(requirements) {}

    virtual ~Schedulable() {}

    template <typename E>
    auto Schedule(E e);

    auto* requirements() { return &requirements_; }

  private:
    Requirements requirements_;
  };

  static StaticThreadPool& Scheduler()
  {
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

  void Submit(Callback<> callback, Context* context, bool defer = true) override;

  template <typename T>
  auto Schedule(Requirements* requirements, T t);

  template <typename F>
  auto Parallel(F f);

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

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename E_, typename Arg_>
struct StaticThreadPoolSchedule : public StaticThreadPool::Waiter
{
  // NOTE: explicit constructor because inheriting 'StaticThreadPool::Waiter'.
  StaticThreadPoolSchedule(
      K_ k,
      StaticThreadPool* pool,
      StaticThreadPool::Requirements* requirements,
      E_ e)
    : StaticThreadPool::Waiter(pool, requirements),
      k_(std::move(k)),
      e_(std::move(e)) {}

  template <typename... Args>
  void Start(Args&&... args)
  {
    static_assert(
        !std::is_void_v<Arg_> || sizeof...(args) == 0,
        "'Schedule' only supports 0 or 1 argument");

    STOUT_EVENTUALS_LOG(1) << "Scheduling '" << name() << "'";

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
      eventuals::fail(k_, "'" + name() + "' required core is > total cores");
    } else {
      // Save parent scheduler/context (even if it's us).
      // if (scheduler_ == nullptr) {
        scheduler_ = Scheduler::Get(&context_);
      // } else {
      //   Context* context = nullptr;
      //   auto* scheduler = Scheduler::Get(&context);
      //   CHECK(scheduler_ == scheduler);
      //   CHECK(context_ == context);
      // }

      if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
        AdaptAndStart(std::forward<Args>(args)...);
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        STOUT_EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

        pool()->Submit(
            [this]() {
              if constexpr (sizeof...(args) > 0) {
                AdaptAndStart(std::move(*arg_));
              } else {
                AdaptAndStart();
              }
            },
            this);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // NOTE: rather than skip the scheduling all together we make sure
    // to support the use case where code wants to "catch" a failure
    // inside of a 'Schedule()' in order to either recover or
    // propagate a different failure.
    //
    // TODO(benh): check if we're already on the correct execution
    // resource and can thus elide copying the arg execute the code
    // immediately instead of doing 'Submit()'.
    //
    // TODO(benh): avoid allocating on heap by storing args in
    // pre-allocated buffer based on composing with Errors.
    // auto* tuple = new std::tuple { &k_, std::forward<Args>(args)... };

    // scheduler_->Submit(
    //     [tuple]() {
    //       std::apply([](auto* k_, auto&&... args) {
    //         eventuals::fail(*k_, std::forward<decltype(args)>(args)...);
    //       },
    //       std::move(*tuple));
    //       delete tuple;
    //     },
    //     this,
    //     /* defer = */ false); // Execute the code immediately if possible.
  }

  void Stop()
  {
    // scheduler_->Submit(
    //     [this]() {
    //       eventuals::stop(k_);
    //     },
    //     context_,
    //     /* defer = */ false); // Execute the code immediately if possible.
  }

  void Register(Interrupt& interrupt)
  {
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  template <typename... Args>
  void AdaptAndStart(Args&&... args)
  {
    if (!adaptor_) {
      adaptor_.reset(
          // NOTE: for now we're assuming usage of something like
          // 'jemalloc' so 'new' should use lock-free and thread-local
          // arenas. Ideally allocating memory during runtime should
          // actually be *faster* because the memory should have
          // better locality for the execution resource being used
          // (i.e., a local NUMA node). However, we should reconsider
          // this design decision if in practice this performance
          // tradeoff is not emperically a benefit.
          new Adaptor_(
              std::move(e_).template k<Arg_>(
                  scheduler_->Reschedule(context_).template k<Value_>(
                      ThenAdaptor<K_> { k_}))));

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }
    }

    eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
  }

  K_ k_;
  E_ e_;

  std::optional<
    std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>> arg_;

  Interrupt* interrupt_ = nullptr;

  // Parent scheduler/context.
  Scheduler* scheduler_ = nullptr;
  Context* context_ = nullptr;

  using Value_ = typename E_::template ValueFrom<Arg_>;

  using Reschedule_ = decltype(scheduler_->Reschedule(context_));

  using Adaptor_ = decltype(
      std::declval<E_>().template k<Arg_>(
          std::declval<Reschedule_>().template k<Value_>(
              std::declval<ThenAdaptor<K_>>())));

  std::unique_ptr<Adaptor_> adaptor_;
};

////////////////////////////////////////////////////////////////////////

template <typename E_>
struct StaticThreadPoolScheduleComposable
{
  template <typename Arg>
  using ValueFrom = typename E_::template ValueFrom<Arg>;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    return StaticThreadPoolSchedule<K, E_, Arg>(
        std::move(k),
        pool_,
        requirements_,
        std::move(e_));
  }

  StaticThreadPool* pool_;
  StaticThreadPool::Requirements* requirements_;
  E_ e_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Parallel_, typename Ended_>
struct StaticThreadPoolParallelAdaptor : public TypeErasedStream
{
  // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
  StaticThreadPoolParallelAdaptor(
      K_ k,
      Parallel_* parallel,
      Ended_ ended)
    : k_(std::move(k)),
      parallel_(parallel),
      ended_(std::move(ended)) {}

  // NOTE: explicit move-constructor because of 'std::once_flag'.
  StaticThreadPoolParallelAdaptor(StaticThreadPoolParallelAdaptor&& that)
    : k_(std::move(that.k_)),
      parallel_(that.parallel_),
      ended_(std::move(that.ended_)) {}

  template <typename... Args>
  void Start(TypeErasedStream& stream, Args&&... args)
  {
    stream_ = &stream;

    parallel_->Start();

    eventuals::succeed(k_, *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(k_);
  }

  void Body(bool next)
  {
    CHECK(next);

    eventuals::next(*CHECK_NOTNULL(stream_));
  }

  void Ended()
  {
    eventuals::start(ended_);
  }

  void Next() override
  {
    // NOTE: we go "down" into egress before going "up" into ingress
    // in order to properly set up 'egress_' so that it can be used to
    // notify once workers start processing (which they can't do until
    // ingress has started which won't occur until calling 'next(stream_)').
    eventuals::body(k_);

    std::call_once(next_, [this]() {
      eventuals::next(*CHECK_NOTNULL(stream_));
    });
  }

  void Done() override
  {
    eventuals::ended(k_);
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;
  Parallel_* parallel_;
  Ended_ ended_;

  TypeErasedStream* stream_ = nullptr;

  std::once_flag next_;
};

////////////////////////////////////////////////////////////////////////

template <typename Parallel_, typename Ended_>
struct StaticThreadPoolParallelAdaptorComposable
{
  template <typename Arg>
  using ValueFrom = void;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    return StaticThreadPoolParallelAdaptor<K, Parallel_, Ended_>(
        std::move(k),
        parallel_,
        std::move(ended_));
  }

  Parallel_* parallel_;
  Ended_ ended_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename Parallel, typename Ended>
auto StaticThreadPoolParallelAdaptor(Parallel* parallel, Ended ended)
{
  return detail::StaticThreadPoolParallelAdaptorComposable<Parallel, Ended> {
    parallel,
    std::move(ended)
  };
}

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename F_, typename Arg_>
struct StaticThreadPoolParallel : public Synchronizable
{
  StaticThreadPoolParallel(F_ f)
    : Synchronizable(&lock_),
      f_(std::move(f)) {}

  StaticThreadPoolParallel(StaticThreadPoolParallel&& that)
    : Synchronizable(&lock_),
      f_(std::move(that.f_)) {}

  ~StaticThreadPoolParallel()
  {
    for (auto* worker : workers_) {
      while (!worker->done.load()) {
        // TODO(benh): donate this thread in case it needs to be used
        // to resume/run a worker?
      }
      delete worker;
    }
  }

  void Start()
  {
    // Add all workers to 'workers_' *before* starting them so that
    // 'workers_' remains read-only.
    auto concurrency = StaticThreadPool::Scheduler().concurrency;
    workers_.reserve(concurrency);
    for (size_t core = 0; core < concurrency; core++) {
      workers_.emplace_back(new Worker(core));
    }

    for (auto* worker : workers_) {
      worker->task.emplace(
          worker,
          [this](auto* worker) {

            // TODO(benh): allocate 'arg' and store the pointer to it
            // so 'ingress' can use it for each item off the stream.

            return eventuals::Acquire(&lock_)
              | eventuals::Repeat(
                  Wait<Arg_>().condition([this, worker](auto& k) mutable {
                    CHECK(Scheduler::Verify(worker));
                    if (!worker->started) {
                      // Overwrite 'notify' so that we'll get
                      // signalled properly. Initially 'notify' does
                      // nothing so that it can get called on ingress
                      // even before the worker has started.
                      worker->notify = [&k]() {
                        eventuals::notify(k);
                      };
                      worker->started = true;
                    }

                    if (ended_) {
                      eventuals::stop(k);
                    } else if (!worker->arg) {
                      worker->waiting = true;
                      eventuals::wait(k);

                      if (++idle_ == 1) {
                        assert(ingress_);
                        ingress_();
                      }

                    } else {
                      worker->waiting = false;
                      eventuals::succeed(k, std::move(*worker->arg));
                    }
                  })
                  | eventuals::Release(&lock_))
              | f_()
              // TODO(benh): 'Until()', 'Map()', 'Loop()' here vs 'Reduce()'?
              | eventuals::Reduce(
                  0,
                  [this, worker](auto&) {
                    return eventuals::Acquire(&lock_)
                      | eventuals::Lambda([this, worker](auto&&... args) {
                        values_.emplace_back(std::forward<decltype(args)>(args)...);

                        assert(egress_);
                        egress_();

                        worker->arg.reset();
                        busy_--;

                        return true;
                      });
                  })
              | (eventuals::Eventual<int>()
                 .start([worker](auto&&...) {
                   // handle done, the only way we got here is if 'f_()' did a 'done()'
                   worker->done.store(true);
                 })
                 .fail([worker](auto&&...) {
                   // handle done, the only way we got here is if 'f_()' did a 'done()'
                   worker->done.store(true);
                 })
                 .stop([worker](auto&&...) {
                   // handle done, the only way we got here is if 'f_()' did a 'done()'
                   worker->done.store(true);
                 }));
          });

      StaticThreadPool::Scheduler().Submit(
          [worker]() {
            assert(worker->task);
            worker->task->Start(
                worker->interrupt,
                [](auto&&... args) {},
                [](std::exception_ptr) {},
                []() {});
          },
          worker);
    }
  }

  StaticThreadPool::Requirements ingress_requirements_;

  auto Ingress()
  {
    ingress_requirements_.preempt = true;
    ingress_requirements_.name = "ingress";
    return eventuals::Map(
        StaticThreadPool::Scheduler().Schedule( // TODO(benh): Preempt() which should not cause the parent scheduler to be 'rescheduled' after executing because there isn't anything to reschedule (it just falls through instead).
            &ingress_requirements_,
            Synchronized(
                // Wait([this](auto notify) {
                //   ingress_ = notify;
                //   return [this]() {
                //     return idle_ == 0 ...;
                //   };
                // })
                Wait<bool>().condition([this](auto& k, auto&&... args) mutable {
                  if (!started_) {
                    ingress_ = [&k]() {
                      eventuals::notify(k);
                    };
                    started_ = false;
                  }

                  CHECK(!ended_);

                  if (idle_ == 0) {
                    return eventuals::wait(k);
                  } else {
                    bool assigned = false;
                    for (auto* worker : workers_) {
                      if (worker->waiting && !worker->arg) {
                        worker->arg.emplace(std::forward<decltype(args)>(args)...);
                        worker->notify();
                        assigned = true;
                        break;
                      }
                    }
                    CHECK(assigned);

                    idle_--;
                    busy_++;

                    eventuals::succeed(k, true);
                  }
                }))));
  }

  auto Egress()
  {
    // NOTE: we put 'Until' up here so that we don't have to copy any
    // values which would be required if it was done after the 'Map'
    // below (this pattern is an argumet for a 'While' construct or
    // something similar).
    return eventuals::Until(Synchronized(Wait<bool>().condition([this](auto& k) {
      if (!egress_) {
        egress_ = [&k]() {
          eventuals::notify(k);
        };
      }

      if (!values_.empty()) {
        eventuals::succeed(k, false); // Not done.
      } else if (busy_ > 0) {
        eventuals::wait(k);
      } else if (!ended_) {
        eventuals::wait(k);
      } else {
        eventuals::succeed(k, true); // Done.
      }
    })))
      | eventuals::Map(Synchronized(eventuals::Lambda([this]() {
        CHECK(!values_.empty());
        auto value = std::move(values_.front());
        values_.pop_front();
        // TODO(benh): use 'Eventual' to avoid extra moves?
        return std::move(value);
      })));
  }

  auto operator()()
  {
    return Ingress()
      | eventuals::StaticThreadPoolParallelAdaptor(
          this,
          (Synchronized(eventuals::Then([this]() {
            ended_ = true;
            for (auto* worker : workers_) {
              worker->notify();
            }
            egress_();
            return Just();
          }))
          | eventuals::Terminal()).template k<void>())
      | Egress();
  }

  F_ f_;

  Lock lock_;

  // TODO(benh): consider whether to use a list, deque, or vector?
  // Currently using a deque assuming it'll give the best performance
  // for the continuation that wants to iterate through each value,
  // but some performance benchmarks should be used to evaluate.
  using Value_ = typename decltype(f_())::template ValueFrom<Arg_>;
  std::deque<Value_> values_;

  // TODO(benh): consider allocating more of worker's fields by the
  // worker itself and/or consider memory alignment of fields in order
  // to limit cache lines bouncing around or false sharing.
  struct Worker : StaticThreadPool::Waiter
  {
    Worker(size_t core)
      : StaticThreadPool::Waiter(
          &StaticThreadPool::Scheduler(),
          &requirements)
    {
      requirements.name = "[worker " + std::to_string(core) + "]";
    }

    StaticThreadPool::Requirements requirements;
    std::optional<Arg_> arg;
    Callback<> notify = [](){}; // Initially a no-op so ingress can call.
    std::optional<Task<int>::With<Worker*>> task;
    Interrupt interrupt;
    bool started = false;
    bool waiting = true; // Initially true so ingress can copy to 'arg'.
    std::atomic<bool> done = false;
  };

  std::vector<Worker*> workers_;

  size_t idle_ = 0;
  size_t busy_ = 0;

  Callback<> ingress_ = [](){}; // Initially a no-op so workers can notify.

  bool started_ = false;

  Callback<> egress_;

  bool ended_ = false;
};

////////////////////////////////////////////////////////////////////////

template <typename F_>
struct StaticThreadPoolParallelComposable
{
  template <typename Arg>
  using ValueFrom = typename std::invoke_result_t<F_>::template ValueFrom<Arg>;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    return eventuals::Closure(StaticThreadPoolParallel<F_, Arg>(std::move(f_)))
      .template k<Arg>(std::move(k));
  }

  F_ f_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedule(Requirements* requirements, E e)
{
  return detail::StaticThreadPoolScheduleComposable<E> {
    this,
    requirements,
    std::move(e)
  };
}

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedulable::Schedule(E e)
{
  return StaticThreadPool::Scheduler().Schedule(
      &requirements_,
      std::move(e));
}

////////////////////////////////////////////////////////////////////////

template <typename F>
auto StaticThreadPool::Parallel(F f)
{
  return detail::StaticThreadPoolParallelComposable<F> { std::move(f) };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
