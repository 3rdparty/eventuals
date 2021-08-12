#pragma once

#include <atomic>
#include <deque>
#include <thread>
#include <vector>

#include "stout/closure.h"
#include "stout/lambda.h"
#include "stout/lock.h"
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
        requirements_(that.requirements_) {}

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

  bool Continue(Context* context) override;

  void Submit(Callback<> callback, Context* context) override;

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
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          if constexpr (!std::is_void_v<Arg_>) {
            arg_.emplace(std::forward<Args>(args)...);
          }

          STOUT_EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                if constexpr (sizeof...(args) > 0) {
                  eventuals::succeed(*adaptor_, std::move(*arg_));
                } else {
                  eventuals::succeed(*adaptor_);
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
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          eventuals::fail(*adaptor_, std::forward<Args>(args)...);
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          // TODO(benh): avoid allocating on heap by storing args in
          // pre-allocated buffer based on composing with Errors.
          auto* tuple = new std::tuple{this, std::forward<Args>(args)...};

          STOUT_EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [tuple]() {
                std::apply(
                    [](auto* schedule, auto&&... args) {
                      schedule->Adapt();
                      eventuals::fail(
                          *schedule->adaptor_,
                          std::forward<decltype(args)>(args)...);
                    },
                    std::move(*tuple));
                delete tuple;
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
        if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
          Adapt();
          auto* previous = Scheduler::Context::Switch(this);
          eventuals::stop(*adaptor_);
          previous = Scheduler::Context::Switch(previous);
          CHECK_EQ(previous, this);
        } else {
          STOUT_EVENTUALS_LOG(1) << "Schedule submitting '" << name() << "'";

          pool()->Submit(
              [this]() {
                Adapt();
                eventuals::stop(*adaptor_);
              },
              this);
        }
      }
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Adapt() {
      if (!adaptor_) {
        // Save previous context (even if it's us).
        Scheduler::Context* previous = Scheduler::Context::Get();

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
                    Reschedule(previous).template k<Value_>(
                        _Then::Adaptor<K_>{k_}))));

        if (interrupt_ != nullptr) {
          adaptor_->Register(*interrupt_);
        }
      }
    }

    K_ k_;
    E_ e_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    Interrupt* interrupt_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adaptor_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<detail::_Reschedule::Composable>()
            .template k<Value_>(std::declval<_Then::Adaptor<K_>>())));

    std::unique_ptr<Adaptor_> adaptor_;
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

struct _StaticThreadPoolParallel {
  struct _IngressAdaptor {
    template <typename K_, typename Parallel_, typename Cleanup_>
    struct Continuation : public TypeErasedStream {
      // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
      Continuation(
          K_ k,
          Parallel_* parallel,
          Cleanup_ cleanup)
        : k_(std::move(k)),
          parallel_(parallel),
          cleanup_(std::move(cleanup)) {}

      // NOTE: explicit move-constructor because of 'std::once_flag'.
      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          parallel_(that.parallel_),
          cleanup_(std::move(that.cleanup_)) {}

      void Start(TypeErasedStream& stream) {
        stream_ = &stream;

        parallel_->done_.store(false);

        parallel_->Start();

        eventuals::succeed(k_, *this);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        // TODO(benh): support failure with no error arguments.
        static_assert(
            sizeof...(args) == 1,
            "'Parallel' currently requires failures to include an error");

        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(std::forward<Args>(args)...);

        eventuals::succeed(cleanup_, std::move(exception));

        parallel_->done_.store(true);
      }

      void Stop() {
        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(StoppedException());

        eventuals::succeed(cleanup_, std::move(exception));

        parallel_->done_.store(true);
      }

      template <typename... Args>
      void Body(Args&&...) {
        eventuals::next(*CHECK_NOTNULL(stream_));
      }

      void Ended() {
        eventuals::succeed(cleanup_, std::optional<std::exception_ptr>());

        parallel_->done_.store(true);
      }

      void Next() override {
        // NOTE: we go "down" into egress before going "up" into
        // ingress in order to properly set up 'egress_' so that it
        // can be used to notify once workers start processing (which
        // they can't do until ingress has started which won't occur
        // until calling 'next(stream_)').
        eventuals::body(k_);

        std::call_once(next_, [this]() {
          eventuals::next(*CHECK_NOTNULL(stream_));
        });
      }

      void Done() override {
        eventuals::done(*CHECK_NOTNULL(stream_));

        eventuals::ended(k_);
      }

      void Register(Interrupt& interrupt) {
        k_.Register(interrupt);
      }

      K_ k_;
      Parallel_* parallel_;
      Cleanup_ cleanup_;

      TypeErasedStream* stream_ = nullptr;

      std::once_flag next_;
    };

    template <typename Parallel_, typename Cleanup_>
    struct Composable {
      template <typename Arg>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K, Parallel_, Cleanup_>(
            std::move(k),
            parallel_,
            std::move(cleanup_));
      }

      Parallel_* parallel_;
      Cleanup_ cleanup_;
    };
  };

  template <typename Parallel, typename E>
  static auto IngressAdaptor(Parallel* parallel, E e) {
    auto cleanup = (std::move(e) | Terminal())
                       .template k<std::optional<std::exception_ptr>>();
    using Cleanup = decltype(cleanup);
    return _IngressAdaptor::Composable<Parallel, Cleanup>{
        parallel,
        std::move(cleanup)};
  }

  struct _EgressAdaptor {
    template <typename K_>
    struct Continuation {
      void Start(TypeErasedStream& stream) {
        eventuals::succeed(k_, stream);
      }

      // NOTE: we should "catch" any failures or stops and save in
      // 'exception_' which we then will "rethrow" during "ended".
      template <typename... Args>
      void Fail(Args&&... args) = delete;
      void Stop() = delete;

      template <typename... Args>
      void Body(Args&&... args) {
        eventuals::body(k_, std::forward<Args>(args)...);
      }

      void Ended() {
        // NOTE: not using synchronization here as "ended" implies
        // that "cleanup" has been observed in a synchronized fashion
        // which implies an exception is either set or not.
        if (exception_) {
          try {
            std::rethrow_exception(*exception_);
          } catch (const StoppedException&) {
            eventuals::stop(k_);
          } catch (...) {
            eventuals::fail(k_, std::current_exception());
          }
        } else {
          eventuals::ended(k_);
        }
      }

      void Register(Interrupt& interrupt) {
        k_.Register(interrupt);
      }

      K_ k_;
      std::optional<std::exception_ptr>& exception_;
    };

    struct Composable {
      template <typename Arg>
      using ValueFrom = Arg;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), exception_};
      }

      std::optional<std::exception_ptr>& exception_;
    };
  };

  static auto EgressAdaptor(std::optional<std::exception_ptr>& exception) {
    return _EgressAdaptor::Composable{exception};
  }

  struct _WorkerAdaptor {
    template <typename K_, typename Cleanup_>
    struct Continuation {
      void Start(TypeErasedStream& stream) {
        stream_ = &stream;
        eventuals::next(*CHECK_NOTNULL(stream_));
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        // TODO(benh): support failure with no error arguments.
        static_assert(
            sizeof...(args) == 1,
            "'Parallel' currently requires failures to include an error");

        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(std::forward<Args>(args)...);

        eventuals::succeed(cleanup_, std::move(exception));

        // TODO(benh): render passing 'Undefined()' unnecessary.
        eventuals::succeed(k_, Undefined());
      }

      void Stop() {
        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(StoppedException());

        eventuals::succeed(cleanup_, std::move(exception));

        // TODO(benh): render passing 'Undefined()' unnecessary.
        eventuals::succeed(k_, Undefined());
      }

      void Body() {
        eventuals::next(*CHECK_NOTNULL(stream_));
      }

      void Ended() {
        // TODO(benh): render passing 'Undefined()' unnecessary.
        eventuals::succeed(k_, Undefined());
      }

      void Register(Interrupt& interrupt) {
        k_.Register(interrupt);
      }

      K_ k_;
      Cleanup_ cleanup_;

      TypeErasedStream* stream_ = nullptr;
    };

    template <typename Cleanup_>
    struct Composable {
      template <typename Arg>
      using ValueFrom = Undefined; // TODO(benh): make this void.

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K, Cleanup_>{std::move(k), std::move(cleanup_)};
      }

      Cleanup_ cleanup_;
    };
  };

  template <typename E>
  static auto WorkerAdaptor(E e) {
    auto cleanup = (std::move(e) | Terminal())
                       .template k<std::optional<std::exception_ptr>>();
    using Cleanup = decltype(cleanup);
    return _WorkerAdaptor::Composable<Cleanup>{std::move(cleanup)};
  }

  template <typename F_, typename Arg_>
  struct Continuation : public Synchronizable {
    Continuation(F_ f)
      : Synchronizable(&lock_),
        f_(std::move(f)) {}

    Continuation(Continuation&& that)
      : Synchronizable(&lock_),
        f_(std::move(that.f_)) {}

    ~Continuation() {
      for (auto* worker : workers_) {
        while (!worker->done.load()) {
          // TODO(benh): donate this thread in case it needs to be used
          // to resume/run a worker?
        }
        delete worker;
      }
      while (!done_.load()) {}
    }

    void Start();

    auto Ingress();
    auto Egress();

    auto operator()() {
      return Ingress()
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
    struct Worker : StaticThreadPool::Waiter {
      Worker(size_t core)
        : StaticThreadPool::Waiter(
            &StaticThreadPool::Scheduler(),
            &requirements),
          requirements("[worker " + std::to_string(core) + "]") {}

      StaticThreadPool::Requirements requirements;
      std::optional<Arg_> arg;
      Callback<> notify = []() {}; // Initially a no-op so ingress can call.
      std::optional<Task<Undefined>::With<Worker*>> task;
      Interrupt interrupt;
      bool waiting = true; // Initially true so ingress can copy to 'arg'.
      std::atomic<bool> done = false;
    };

    std::vector<Worker*> workers_;

    size_t idle_ = 0;
    size_t busy_ = 0;

    Callback<> ingress_ = []() {}; // Initially a no-op so workers can notify.

    Callback<> egress_;

    bool cleanup_ = false;
    std::atomic<bool> done_ = true; // Toggled to 'false' when we start!
    std::optional<std::exception_ptr> exception_;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom =
        typename std::invoke_result_t<F_>::template ValueFrom<Arg>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Closure(Continuation<F_, Arg>(std::move(f_)))
          .template k<Arg>(std::move(k));
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F_, typename Arg_>
void _StaticThreadPoolParallel::Continuation<F_, Arg_>::Start() {
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

          return Acquire(&lock_)
              | Repeat(
                     Wait([this, worker](auto notify) mutable {
                       // Overwrite 'notify' so that we'll get
                       // signalled properly. Initially 'notify' does
                       // nothing so that it can get called on ingress
                       // even before the worker has started.
                       worker->notify = std::move(notify);

                       return [this, worker]() mutable {
                         CHECK_EQ(worker, Scheduler::Context::Get());
                         if (cleanup_) {
                           if (worker->arg) {
                             busy_--;
                           }
                           return false;
                         } else if (!worker->arg) {
                           worker->waiting = true;

                           if (++idle_ == 1) {
                             assert(ingress_);
                             ingress_();
                           }

                           return true;
                         } else {
                           worker->waiting = false;
                           return false;
                         }
                       };
                     }))
              | Until(
                     Lambda([this]() {
                       return cleanup_;
                     })
                     | Release(&lock_))
              | Map(
                     // TODO(benh): create 'Move()' like abstraction that does:
                     Eventual<Arg_>()
                         .start([worker](auto& k) {
                           eventuals::succeed(k, std::move(*worker->arg));
                         })
                     | f_()
                     | Acquire(&lock_)
                     | Lambda(
                         [this, worker](auto&&... args) {
                           values_.emplace_back(
                               std::forward<decltype(args)>(args)...);

                           assert(egress_);
                           egress_();

                           worker->arg.reset();
                           busy_--;
                         }))
              | WorkerAdaptor(
                     Lambda([this](auto exception) {
                       // First fail/stop wins the "cleanup" rather
                       // than us aggregating all of the fail/stops
                       // that occur.
                       if (!cleanup_) {
                         cleanup_ = true;
                         if (exception) {
                           exception_ = std::move(exception);
                         }
                         for (auto* worker : workers_) {
                           worker->notify();
                         }
                         ingress_();
                       }

                       busy_--; // Used by "egress" to stop waiting.
                       egress_();
                     })
                     | Release(&lock_));
        });

    StaticThreadPool::Scheduler().Submit(
        [worker]() {
          assert(worker->task);
          worker->task->Start(
              worker->interrupt,
              [worker](auto&&...) {
                worker->done.store(true);
              },
              [](std::exception_ptr) {
                LOG(FATAL) << "Unreachable";
              },
              []() {
                LOG(FATAL) << "Unreachable";
              });
        },
        worker);
  }
}

////////////////////////////////////////////////////////////////////////

template <typename F_, typename Arg_>
auto _StaticThreadPoolParallel::Continuation<F_, Arg_>::Ingress() {
  return Until(Preempt(
             "ingress",
             Synchronized(
                 Wait([this](auto notify) mutable {
                   ingress_ = std::move(notify);
                   return [this](auto&&... args) mutable {
                     if (cleanup_) {
                       return false;
                     } else if (idle_ == 0) {
                       return true;
                     } else {
                       bool assigned = false;
                       for (auto* worker : workers_) {
                         if (worker->waiting && !worker->arg) {
                           worker->arg.emplace(
                               std::forward<decltype(args)>(args)...);
                           worker->notify();
                           assigned = true;
                           break;
                         }
                       }
                       CHECK(assigned);

                       idle_--;
                       busy_++;

                       return false;
                     }
                   };
                 })
                 | Lambda([this](auto&&...) {
                     return cleanup_;
                   }))))
      | IngressAdaptor(
             this,
             Synchronized(Lambda([this](auto exception) {
               if (!cleanup_) {
                 cleanup_ = true;
                 if (exception) {
                   exception_ = std::move(exception);
                 }
                 for (auto* worker : workers_) {
                   worker->notify();
                 }
                 egress_();
               }
             })));
}

////////////////////////////////////////////////////////////////////////

template <typename F_, typename Arg_>
auto _StaticThreadPoolParallel::Continuation<F_, Arg_>::Egress() {
  // NOTE: we put 'Until' up here so that we don't have to copy any
  // values which would be required if it was done after the 'Map'
  // below (this pattern is an argumet for a 'While' construct or
  // something similar).
  return Until(
             Synchronized(
                 Wait([this](auto notify) {
                   egress_ = std::move(notify);
                   return [this]() {
                     if (!values_.empty()) {
                       return false;
                     } else {
                       return busy_ > 0 || !cleanup_;
                     }
                   };
                 })
                 | Lambda([this]() {
                     return values_.empty() && !busy_ && cleanup_;
                   })))
      | Map(Synchronized(Lambda([this]() {
           CHECK(!values_.empty());
           auto value = std::move(values_.front());
           values_.pop_front();
           // TODO(benh): use 'Eventual' to avoid extra moves?
           return std::move(value);
         })))
      | EgressAdaptor(exception_);
}

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedule(Requirements* requirements, E e) {
  return detail::_StaticThreadPoolSchedule::Composable<E>{
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

template <typename F>
auto StaticThreadPool::Parallel(F f) {
  return detail::_StaticThreadPoolParallel::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
