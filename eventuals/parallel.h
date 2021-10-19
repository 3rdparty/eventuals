#pragma once

#include <atomic>
#include <deque>
#include <vector>

#include "eventuals/closure.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/static-thread-pool.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Uses the eventual returned from 'f()' to run each item in the
// stream in parallel.
//
// NOTE: current implementation relies on 'StaticThreadPool' for the
// actual parallelization.
template <typename F>
auto Parallel(F f);

////////////////////////////////////////////////////////////////////////

struct _Parallel {
  struct _IngressAdaptor {
    template <typename K_, typename Cleanup_>
    struct Continuation : public detail::TypeErasedStream {
      // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
      Continuation(K_ k, Cleanup_ cleanup)
        : k_(std::move(k)),
          cleanup_(std::move(cleanup)) {}

      // NOTE: explicit move-constructor because of 'std::once_flag'.
      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          cleanup_(std::move(that.cleanup_)) {}

      void Start(detail::TypeErasedStream& stream) {
        stream_ = &stream;

        k_.Start(*this);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        std::optional<std::exception_ptr> exception = [&]() {
          if constexpr (sizeof...(args) > 0) {
            return std::make_exception_ptr(std::forward<Args>(args)...);
          } else {
            return std::make_exception_ptr(
                std::runtime_error("ingress failed (without an error)"));
          }
        }();

        cleanup_.Start(std::move(exception));
      }

      void Stop() {
        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(StoppedException());

        cleanup_.Start(std::move(exception));
      }

      template <typename... Args>
      void Body(Args&&...) {
        // NOTE: only one of 'Next()' or 'Done()' should be made into
        // ingress at a time, which we manage using an atomic status.
        auto expected = status_.load();

        // We might have arrived here from a previous call to 'Body()'
        // which called 'Next()' and then made it back here. In this
        // case, we simply make the call to 'Next()' and return,
        // knowing that initial previous call to 'Body()' will handle
        // invoking 'Done()' if necessary.
        if (expected == Status::Next) {
          CHECK_NOTNULL(stream_)->Next();
        } else {
          while (expected != Status::Done) {
            CHECK(Status::Idle == expected);

            if (status_.compare_exchange_weak(expected, Status::Next)) {
              CHECK_NOTNULL(stream_)->Next();

              expected = Status::Next;
              if (!status_.compare_exchange_strong(expected, Status::Idle)) {
                CHECK(Status::Done == expected);
                CHECK_NOTNULL(stream_)->Done();
              }

              break;
            }
          }
        }
      }

      void Ended() {
        cleanup_.Start(std::optional<std::exception_ptr>());
      }

      void Next() override {
        // NOTE: we go "down" into egress before going "up" into
        // ingress in order to properly set up 'egress_' so that it
        // can be used to notify once workers start processing (which
        // they can't do until ingress has started which won't occur
        // until calling 'next(stream_)').
        k_.Body();

        // We only want to "start" ingress once here and let 'Body()'
        // above continue calling 'Next()' as there are available
        // workers, hence the use of 'std::call_once'.
        std::call_once(next_, [this]() {
          Body();
        });
      }

      void Done() override {
        // NOTE: only one of 'Next()' or 'Done()' should be made into
        // ingress at a time, which we manage with the following.
        auto expected = Status::Idle;
        while (!status_.compare_exchange_weak(expected, Status::Done)) {}
        if (expected == Status::Idle) {
          CHECK_NOTNULL(stream_)->Done();
        }

        k_.Ended();
      }

      void Register(Interrupt& interrupt) {
        k_.Register(interrupt);
      }

      K_ k_;
      Cleanup_ cleanup_;

      detail::TypeErasedStream* stream_ = nullptr;

      std::once_flag next_;

      // Need a "status" flag for making sure that only one of
      // 'Next()' or 'Done()' is being called into ingress at a time.
      enum class Status {
        Idle,
        Next,
        Done,
      };

      std::atomic<Status> status_ = Status::Idle;
    };

    template <typename Cleanup_>
    struct Composable {
      template <typename Arg>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K, Cleanup_>(std::move(k), std::move(cleanup_));
      }

      Cleanup_ cleanup_;
    };
  };

  template <typename E>
  static auto IngressAdaptor(E e) {
    auto cleanup = (std::move(e) | Terminal())
                       .template k<std::optional<std::exception_ptr>>();
    using Cleanup = decltype(cleanup);
    return _IngressAdaptor::Composable<Cleanup>{std::move(cleanup)};
  }

  struct _EgressAdaptor {
    template <typename K_>
    struct Continuation {
      void Start(detail::TypeErasedStream& stream) {
        k_.Start(stream);
      }

      // NOTE: we should "catch" any failures or stops and save in
      // 'exception_' which we then will "rethrow" during "ended".
      template <typename... Args>
      void Fail(Args&&... args) = delete;
      void Stop() = delete;

      template <typename... Args>
      void Body(Args&&... args) {
        k_.Body(std::forward<Args>(args)...);
      }

      void Ended() {
        // NOTE: not using synchronization here as "ended" implies
        // that "cleanup" has been observed in a synchronized fashion
        // which implies an exception is either set or not.
        if (exception_) {
          try {
            std::rethrow_exception(*exception_);
          } catch (const StoppedException&) {
            k_.Stop();
          } catch (...) {
            k_.Fail(std::current_exception());
          }
        } else {
          k_.Ended();
        }

        // NOTE: after setting 'done' we can no longer reference
        // 'exception_' (or any other variables we end up capturing).
        done_.store(true);
      }

      void Register(Interrupt& interrupt) {
        k_.Register(interrupt);
      }

      K_ k_;
      std::optional<std::exception_ptr>& exception_;
      std::atomic<bool>& done_;
    };

    struct Composable {
      template <typename Arg>
      using ValueFrom = Arg;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), exception_, done_};
      }

      std::optional<std::exception_ptr>& exception_;
      std::atomic<bool>& done_;
    };
  };

  static auto EgressAdaptor(
      std::optional<std::exception_ptr>& exception,
      std::atomic<bool>& done) {
    return _EgressAdaptor::Composable{exception, done};
  }

  struct _WorkerAdaptor {
    template <typename K_, typename Cleanup_>
    struct Continuation {
      void Start(detail::TypeErasedStream& stream) {
        stream_ = &stream;
        CHECK_NOTNULL(stream_)->Next();
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        // TODO(benh): support failure with no error arguments.
        static_assert(
            sizeof...(args) == 1,
            "'Parallel' currently requires failures to include an error");

        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(std::forward<Args>(args)...);

        cleanup_.Start(std::move(exception));

        // TODO(benh): render passing 'Undefined()' unnecessary.
        k_.Start(Undefined());
      }

      void Stop() {
        std::optional<std::exception_ptr> exception =
            std::make_exception_ptr(StoppedException());

        cleanup_.Start(std::move(exception));

        // TODO(benh): render passing 'Undefined()' unnecessary.
        k_.Start(Undefined());
      }

      void Body() {
        CHECK_NOTNULL(stream_)->Next();
      }

      void Ended() {
        // TODO(benh): render passing 'Undefined()' unnecessary.
        k_.Start(Undefined());
      }

      void Register(Interrupt& interrupt) {
        k_.Register(interrupt);
      }

      K_ k_;
      Cleanup_ cleanup_;

      detail::TypeErasedStream* stream_ = nullptr;
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
      : f_(std::move(f)) {}

    Continuation(Continuation&& that)
      : Synchronizable(),
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
      done_.store(false);

      // NOTE: we eagerly start up the workers so that they may be
      // ready when we get the first item from the stream however if
      // the stream ends up returning no items than we'll have
      // performed unnecessary computation and used unnecessary
      // resources.
      Start();

      return Ingress()
          | Egress();
    }

    F_ f_;

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
          requirements(
              "[worker " + std::to_string(core) + "]",
              Pinned(core)) {}

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
void _Parallel::Continuation<F_, Arg_>::Start() {
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

          return Acquire(lock())
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
              | Until([this]() {
                   return Just(cleanup_)
                       | Release(lock());
                 })
              | Map(
                     // TODO(benh): create 'Move()' like abstraction that does:
                     Eventual<Arg_>()
                         .start([worker](auto& k) {
                           k.Start(std::move(*worker->arg));
                         })
                     | f_()
                     | Acquire(lock())
                     | Then(
                         [this, worker](auto&&... args) {
                           values_.emplace_back(
                               std::forward<decltype(args)>(args)...);

                           assert(egress_);
                           egress_();

                           worker->arg.reset();
                           busy_--;
                         }))
              | WorkerAdaptor(
                     Then([this](auto exception) {
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
                     | Release(lock()));
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
auto _Parallel::Continuation<F_, Arg_>::Ingress() {
  return Map(Preempt(
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
                 | Then([this](auto&&...) {
                     return cleanup_;
                   }))))
      | Until([](bool cleanup) {
           return cleanup;
         })
      | IngressAdaptor(
             Synchronized(Then([this](auto exception) {
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
auto _Parallel::Continuation<F_, Arg_>::Egress() {
  // NOTE: we put 'Until' up here so that we don't have to copy any
  // values which would be required if it was done after the 'Map'
  // below (this pattern is an argumet for a 'While' construct or
  // something similar).
  return Map(Synchronized(
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
             | Then([this]() {
                 return values_.empty() && !busy_ && cleanup_;
               })))
      | Until([](bool done) {
           return done;
         })
      | Map(Synchronized(Then([this](auto&&) {
           CHECK(!values_.empty());
           auto value = std::move(values_.front());
           values_.pop_front();
           // TODO(benh): use 'Eventual' to avoid extra moves?
           return std::move(value);
         })))
      | EgressAdaptor(exception_, done_);
}

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Parallel(F f) {
  return _Parallel::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
