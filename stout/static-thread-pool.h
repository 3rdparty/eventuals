#pragma once

#include <atomic>
#include <deque>
#include <thread>
#include <vector>

#include "stout/just.h"
#include "stout/scheduler.h"
#include "stout/semaphore.h"
#include "stout/then.h"

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
  };

  struct Waiter
  {
  public:
    Waiter(StaticThreadPool* pool, Requirements* requirements)
      : pool_(pool), requirements_(requirements) {}

    auto* pool() { return pool_; }
    auto* requirements() { return requirements_; }

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

    template <typename F>
    auto Schedule(F f);

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

  void Submit(Callback<> callback, void* context, bool defer = true) override
  {
    assert(context != nullptr);

    auto* waiter = static_cast<Waiter*>(context);

    auto& pinned = waiter->requirements()->pinned;

    assert(pinned.core);

    unsigned int core = pinned.core.value();

    assert(core < concurrency);

    if (!defer && StaticThreadPool::member && StaticThreadPool::core == core) {
      callback();
    } else {
      waiter->callback = std::move(callback);

      auto* head = heads_[core];

      waiter->next = head->load(std::memory_order_relaxed);

      while (!head->compare_exchange_weak(
                 waiter->next, waiter,
                 std::memory_order_release,
                 std::memory_order_relaxed));

      auto* semaphore = semaphores_[core];

      semaphore->Signal();
    }
  }

  template <typename F>
  auto Schedule(Requirements* requirements, F f);

private:
  // NOTE: in case there is a future debate on why we use semaphore vs
  // something like eventfd for "signalling" the thread see
  // https://stackoverflow.com/q/9826919
  std::vector<Semaphore*> semaphores_;
  std::vector<std::atomic<Waiter*>*> heads_;
  std::deque<Semaphore> ready_;
  std::vector<std::thread> threads_;
  std::atomic<bool> shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_ = Undefined>
struct StaticThreadPoolSchedule : public StaticThreadPool::Waiter
{
  using E_ = typename InvokeResultPossiblyUndefined<F_, Arg_>::type;

  using Value = typename ValueFrom<
    K_,
    typename ValuePossiblyUndefined<E_>::Value>::type;

  StaticThreadPoolSchedule(
      K_ k,
      StaticThreadPool* pool,
      StaticThreadPool::Requirements* requirements,
      F_ f)
    : StaticThreadPool::Waiter(pool, requirements),
      k_(std::move(k)),
      f_(std::move(f)) {}

  template <typename Arg, typename K, typename F>
  static auto create(
      K k,
      StaticThreadPool* pool,
      StaticThreadPool::Requirements* requirements,
      F f)
  {
    return StaticThreadPoolSchedule<K, F, Arg>(
        std::move(k),
        pool,
        requirements,
        std::move(f));
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
        pool(),
        requirements(),
        std::move(f_));
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
    // static_assert(std::is_invocable_v<decltype(f), decltype(args)...>);

    // static_assert(IsContinuation<decltype(f(args...))>::value);

    static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                  "Schedule only supports 0 or 1 argument, but found > 1");

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
      eventuals::fail(k_, "Required core is > total cores");
    } else {
      // Save parent scheduler/context (even if it's us).
      scheduler_ = Scheduler::Get(&context_);

      if (StaticThreadPool::member && StaticThreadPool::core == pinned.core) {
        AdaptAndStart(std::forward<Args>(args)...);
      } else {
        if constexpr (sizeof...(args) > 0) {
          arg_.emplace(std::forward<Args>(args)...);
        }

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
    // NOTE: for now we're assuming usage of something like 'jemalloc'
    // so 'new' will use lock-free thread-local arenas. Also, after
    // the initial allocation we use placement new in order to reuse
    // the already allocated memory if/when 'Start()' is called again
    // (which may happen, for example, if this is a continuation of a
    // stream/generator).
    if (!adaptor_) {
      adaptor_.reset(
          new Adaptor_(
              f_(std::forward<Args>(args)...)
              | scheduler_->Reschedule(context_)
              | Adaptor<K_, typename E_::Value>(
                  k_,
                  [](auto& k_, auto&&... values) {
                    eventuals::succeed(
                        k_,
                        std::forward<decltype(values)>(values)...);
                  })));
    } else {
      adaptor_->~Adaptor_();
      new (adaptor_.get()) Adaptor_(
          f_(std::forward<Args>(args)...)
          | scheduler_->Reschedule(context_)
          | Adaptor<K_, typename E_::Value>(
              k_,
              [](auto& k_, auto&&... values) {
                eventuals::succeed(
                    k_,
                    std::forward<decltype(values)>(values)...);
              }));
    }

    if (interrupt_ != nullptr) {
      adaptor_->Register(*interrupt_);
    }

    eventuals::succeed(*adaptor_);
  }

  K_ k_;
  F_ f_;
  std::optional<Arg_> arg_;

  Interrupt* interrupt_ = nullptr;

  // Parent scheduler/context.
  Scheduler* scheduler_ = nullptr;
  void* context_ = nullptr;

  using Adaptor_ = typename EKPossiblyUndefined<
    E_,
    typename EKPossiblyUndefined<
      decltype(scheduler_->Reschedule(context_)),
      Adaptor<K_, typename ValuePossiblyUndefined<E_>::Value>>::type>::type;

  std::unique_ptr<Adaptor_> adaptor_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct IsContinuation<
  detail::StaticThreadPoolSchedule<K, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct HasTerminal<
  detail::StaticThreadPoolSchedule<K, F, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg_>
struct Compose<detail::StaticThreadPoolSchedule<K, F, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::StaticThreadPoolSchedule<K, F, Arg_> schedule)
  {
    if constexpr (!IsUndefined<Arg>::value) {
      using E = decltype(std::declval<F>()(std::declval<Arg>()));

      static_assert(
          IsContinuation<E>::value,
          "expecting eventual continuation as "
          "result of callable passed to 'Schedule'");

      using Value = typename E::Value;

      auto k = eventuals::compose<Value>(std::move(schedule.k_));
      return detail::StaticThreadPoolSchedule<decltype(k), F, Arg>(
        std::move(k),
        schedule.pool(),
        schedule.requirements(),
        std::move(schedule.f_));
    } else {
      return std::move(schedule);
    }
  }
};

////////////////////////////////////////////////////////////////////////

StaticThreadPool::StaticThreadPool()
  : concurrency(std::thread::hardware_concurrency())
{
  semaphores_.reserve(concurrency);
  heads_.reserve(concurrency);
  threads_.reserve(concurrency);
  for (size_t i = 0; i < concurrency; i++) {
    semaphores_.emplace_back();
    heads_.emplace_back();
    ready_.emplace_back();
    threads_.emplace_back(
        [this, i]() {
          StaticThreadPool::member = true;
          StaticThreadPool::core = i;

          // NOTE: we store each 'semaphore' and 'head' in each thread
          // so as to hopefully get less false sharing when other
          // threads are trying to enqueue a waiter.
          Semaphore semaphore;
          std::atomic<Waiter*> head = nullptr;

          semaphores_[i] = &semaphore;
          heads_[i] = &head;

          ready_[i].Signal();

          do {
            semaphore.Wait();

          load:
            auto* waiter = head.load(std::memory_order_relaxed);

            if (waiter != nullptr) {
              if (waiter->next == nullptr) {
                if (!head.compare_exchange_weak(
                        waiter, nullptr,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                  goto load; // Try again.
                }
              } else {
                while (waiter->next->next != nullptr) {
                  waiter = waiter->next;
                }

                assert(waiter->next != nullptr);

                auto* next = waiter->next;
                waiter->next = nullptr;
                waiter = next;
              }

              assert(waiter->next == nullptr);

              Scheduler::Set(this, waiter);
              assert(waiter->callback);
              waiter->callback();
            }
          } while (!shutdown_.load());
        });
  }

  for (size_t i = 0; i < concurrency; i++) {
    ready_[i].Wait();
  }
}

////////////////////////////////////////////////////////////////////////

StaticThreadPool::~StaticThreadPool()
{
  shutdown_.store(true);
  while (!threads_.empty()) {
    auto* semaphore = semaphores_.back();
    semaphore->Signal();
    semaphores_.pop_back();
    auto& thread = threads_.back();
    thread.join();
    threads_.pop_back();
  }
}

////////////////////////////////////////////////////////////////////////

template <typename F>
auto StaticThreadPool::Schedule(Requirements* requirements, F f)
{
  return detail::StaticThreadPoolSchedule<Undefined, F, Undefined>(
      Undefined(),
      this,
      requirements,
      std::move(f));
}

////////////////////////////////////////////////////////////////////////

template <typename F>
auto StaticThreadPool::Schedulable::Schedule(F f)
{
  return StaticThreadPool::Scheduler().Schedule(
      &requirements_,
      std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
