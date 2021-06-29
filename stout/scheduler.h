#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "stout/callback.h"
#include "stout/conditional.h"
#include "stout/continuation.h"
#include "stout/raise.h"
#include "stout/return.h"
#include "stout/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Arg_>
struct Schedule;

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

class Scheduler
{
public:
  static void Set(Scheduler* scheduler, void* context = nullptr)
  {
    scheduler_ = scheduler;
    context_ = context;
  }

  static Scheduler* Get(void** context)
  {
    assert(scheduler_ != nullptr);
    *context = context_;
    return scheduler_;
  }

  virtual void Submit(Callback<> callback, void* context, bool defer = true)
  {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    callback();
  }

  detail::Schedule<Undefined, Undefined> Schedule(void* context);

private:
  static Scheduler default_;
  static thread_local Scheduler* scheduler_;
  static thread_local void* context_;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Arg_>
struct Schedule
{
  using Value = typename ValueFrom<K_, Arg_>::type;

  Schedule(K_ k, Scheduler* scheduler, void* context)
    : k_(std::move(k)), scheduler_(scheduler), context_(context) {}

  template <typename Arg, typename K>
  static auto create(K k, Scheduler* scheduler, void* context)
  {
    return Schedule<K, Arg>(std::move(k), scheduler, context);
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
   return create<Arg_>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        scheduler_,
        context_);
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
    static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                  "Schedule only supports 0 or 1 argument, but found > 1");

    static_assert(IsUndefined<Arg_>::value || sizeof...(args) == 1);

    if constexpr (sizeof...(args) == 1) {
      assert(!arg_);
      arg_.emplace(std::forward<Args>(args)...);
    }

    scheduler_->Submit(
        [this]() {
          if constexpr (sizeof...(args) == 1) {
            eventuals::succeed(k_, std::move(*arg_));
          } else {
            eventuals::succeed(k_);
          }
        },
        context_,
        /* defer = */ false); // Execute the code immediately if possible.
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): avoid allocating in the heap by storing args in
    // pre-allocated buffer based on tracking Errors...
    fail_ = [tuple = new std::tuple { std::forward<Args>(args)... }](K_* k_) {
      std::apply([&](auto&&... args) {
        eventuals::fail(*k_, std::forward<decltype(args)>(args)...);
      },
      std::move(*tuple));
      delete tuple;
    };

    scheduler_->Submit(
        [this]() {
          fail_(&k_);
        },
        context_,
        /* defer = */ false); // Execute the code immediately if possible.
  }

  void Stop()
  {
    scheduler_->Submit(
        [this]() {
          eventuals::stop(k_);
        },
        context_,
        /* defer = */ false); // Execute the code immediately if possible.
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;
  Scheduler* scheduler_;
  void* context_;
  std::optional<Arg_> arg_;
  Callback<K_*> fail_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg>
struct IsContinuation<
  detail::Schedule<K, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg>
struct HasTerminal<
  detail::Schedule<K, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg_>
struct Compose<detail::Schedule<K, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Schedule<K, Arg_> schedule)
  {
    auto k = eventuals::compose<Arg>(std::move(schedule.k_));
    return detail::Schedule<decltype(k), Arg>(
        std::move(k),
        schedule.scheduler_,
        schedule.context_);
  }
};

////////////////////////////////////////////////////////////////////////

inline detail::Schedule<Undefined, Undefined> Scheduler::Schedule(void* context)
{
  return detail::Schedule<Undefined, Undefined>(Undefined(), this, context);
}

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
  };

  class Schedulable
  {
  public:
    Schedulable(Requirements requirements = Requirements())
      : requirements_(requirements) {}

    template <typename E>
    auto Schedule(E e);

  private:
    Requirements requirements_;
  };

  static StaticThreadPool& Scheduler()
  {
    static StaticThreadPool pool;
    return pool;
  }

  StaticThreadPool()
    : concurrency_(std::thread::hardware_concurrency())
  {
    for (size_t i = 0; i < concurrency_; i++) {
      mutexes_.emplace_back();
      cvs_.emplace_back();
      queues_.emplace_back();
      threads_.emplace_back(
          [this, i]() {
            StaticThreadPool::member = true;
            StaticThreadPool::core = i;

            auto& mutex = mutexes_[i];
            auto& cv = cvs_[i];
            auto& queue = queues_[i];
            std::unique_lock<std::mutex> lock(mutex);
            do {
              cv.wait(lock, [&]() {
                if (shutdown_.load()) {
                  return true;
                } else if (queue.empty()) {
                  return false;
                } else {
                  return true;
                }
              });
              if (!shutdown_.load()) {
                auto& [callback, requirements] = queue.front();
                lock.unlock();
                Scheduler::Set(this, requirements);
                assert(callback);
                callback();
                lock.lock();
                queue.pop_front();
              }
            } while (!shutdown_.load());
          });
    }
  }

  ~StaticThreadPool()
  {
    shutdown_.store(true);
    while (!threads_.empty()) {
      auto& cv = cvs_.back();
      cv.notify_one();
      cvs_.pop_back();
      auto& thread = threads_.back();
      thread.join();
      threads_.pop_back();
    }
  }

  void Submit(Callback<> callback, void* context, bool defer) override
  {
    assert(context != nullptr);

    auto* requirements = static_cast<Requirements*>(context);

    auto& pinned = requirements->pinned;

    if (!pinned.core) {
      // TODO(benh): pick the least loaded core.
      pinned.core = 0;
    }

    assert(pinned.core);

    unsigned int core = pinned.core.value();

    assert(core < concurrency_);

    if (!defer && StaticThreadPool::core == core) {
      callback();
    } else {
      std::unique_lock<std::mutex> lock(mutexes_[core]);

      queues_[core].push_back(std::tuple { std::move(callback), requirements });    

      cvs_[core].notify_one();
    }
  }

  auto Schedule(Requirements* requirements)
  {
    // return eventuals::Eventual<>()
    //   .start([this, requirements](auto& k, auto&&... args) {
    //     Submit([&k]() {
    //       eventuals::succeed(k, 
          
    //     } else {
    //       eventuals::fail(k, "Required core is > total cores");
    //     }
    //   });

    std::optional<unsigned int> core;

    if (requirements->pinned.core) {
      core = requirements->pinned.core.value();
    }

    return Conditional(
        [this, core](auto&&...) {
          return !core || *core < concurrency_;
        },
        [this, requirements](auto&&... args) {
          static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                        "Schedule only supports 0 or 1 argument, but found > 1");
          if constexpr (sizeof...(args) == 0) {
             return Scheduler::Schedule(requirements);
          } else {
            return Scheduler::Schedule(requirements)
              | Return(std::forward<decltype(args)>(args)...);
          }
        },
        [](auto&&... args) {
          static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                        "Schedule only supports 0 or 1 argument, but found > 1");
          if constexpr (sizeof...(args) == 0) {
            return Raise("Required core is > total cores");
          } else {
            return Raise("Required core is > total cores")
              | Return(std::forward<decltype(args)>(args)...);
          }
        });
  }

  static thread_local bool member; // Is thread a member of the static pool?
  static thread_local unsigned int core; // If 'member', for which core?

private:
  const unsigned int concurrency_;
  std::deque<std::mutex> mutexes_;
  std::deque<std::condition_variable> cvs_;
  std::deque<std::deque<std::tuple<Callback<>, Requirements*>>> queues_;
  std::deque<std::thread> threads_;
  std::atomic<bool> shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto StaticThreadPool::Schedulable::Schedule(E e)
{
  void* context = nullptr;
  auto* scheduler = Scheduler::Get(&context);
  return StaticThreadPool::Scheduler().Schedule(&requirements_)
    | std::move(e)
    | scheduler->Schedule(context);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
