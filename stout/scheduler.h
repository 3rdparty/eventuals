#pragma once

#include <optional>
#include <string>
#include <tuple>

#include "stout/callback.h"
#include "stout/compose.h"
#include "stout/interrupt.h"
#include "stout/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Scheduler {
 public:
  struct Context {
    static void Set(Context* context) {
      current_ = context;
    }

    static Context* Get() {
      return current_;
    }

    static Context* Switch(Context* context) {
      Context* previous = current_;
      current_ = context;
      return previous;
    }

    Context(Scheduler* scheduler, std::string* name)
      : scheduler_(scheduler),
        name_(name) {}

    Context(Context&& that) = delete;

    const std::string& name() {
      return *CHECK_NOTNULL(name_);
    }

    template <typename F>
    void Unblock(F f) {
      scheduler_->Submit(std::move(f), this, /* defer = */ true);
    }

    template <typename F>
    void Continue(F&& f) {
      if (scheduler_->Continue(this)) {
        auto* previous = Switch(this);
        f();
        Switch(previous);
      } else {
        scheduler_->Submit(std::move(f), this, /* defer = */ false);
      }
    }

    template <typename F, typename G>
    void Continue(F&& f, G&& g) {
      if (scheduler_->Continue(this)) {
        auto* previous = Switch(this);
        f();
        Switch(previous);
      } else {
        scheduler_->Submit(g(), this, /* defer = */ false);
      }
    }

    // Returns an eventual which will do a 'Scheduler::Submit()' using
    // this context and 'defer = false' in order to continue execution
    // using the execution resource associated with this context.
    auto Reschedule();

    auto* scheduler() {
      return scheduler_;
    }

   private:
    static thread_local Context* current_;

    Scheduler* scheduler_;
    std::string* name_;
  };

  static Scheduler* Default() {
    return default_;
  }

  virtual bool Continue(Context* context) {
    CHECK_EQ(default_, this);
    return Context::Get()->scheduler() == this;
  }

  virtual void Submit(
      Callback<> callback,
      Context* context,
      bool defer = true) {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    CHECK_EQ(default_, this);

    Context* previous = Context::Switch(context);

    STOUT_EVENTUALS_LOG(1)
        << "'" << context->name() << "' preempted '" << previous->name() << "'";

    callback();

    CHECK_EQ(context, Context::Get());

    Context::Switch(previous);
  }

 private:
  static Scheduler* default_;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Reschedule {
  template <typename K_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      context_->Continue(
          [&]() {
            eventuals::succeed(k_, std::forward<Args>(args)...);
          },
          [&]() {
            static_assert(
                sizeof...(args) == 0 || sizeof...(args) == 1,
                "Reschedule only supports 0 or 1 argument, but found > 1");

            static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

            if constexpr (!std::is_void_v<Arg_>) {
              arg_.emplace(std::forward<Args>(args)...);
            }

            STOUT_EVENTUALS_LOG(1)
                << "Reschedule submitting '" << context_->name() << "'";

            return [this]() {
              if constexpr (sizeof...(args) == 1) {
                eventuals::succeed(k_, std::move(*arg_));
              } else {
                eventuals::succeed(k_);
              }
            };
          });
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      context_->Continue(
          [&]() {
            eventuals::fail(k_, std::forward<Args>(args)...);
          },
          [&]() {
            // TODO(benh): avoid allocating on heap by storing args in
            // pre-allocated buffer based on composing with Errors.
            auto* tuple = new std::tuple{&k_, std::forward<Args>(args)...};
            return [tuple]() {
              std::apply(
                  [](auto* k_, auto&&... args) {
                    eventuals::fail(*k_, std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
              delete tuple;
            };
          });
    }

    void Stop() {
      context_->Continue([this]() {
        eventuals::stop(k_);
      });
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
    Scheduler::Context* context_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k), context_};
    }

    Scheduler::Context* context_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

inline auto Scheduler::Context::Reschedule() {
  return detail::_Reschedule::Composable{this};
}

////////////////////////////////////////////////////////////////////////

// Helper for exposing continuations that might need to get
// rescheduled before being executed.
template <typename K_, typename Arg_>
struct Reschedulable {
  auto& operator()() {
    if (!continuation_) {
      previous_ = Scheduler::Context::Get();
      continuation_.emplace(
          previous_->Reschedule().template k<Arg_>(std::move(k_)));

      if (interrupt_ != nullptr) {
        continuation_->Register(*interrupt_);
      }
    }

    CHECK_EQ(Scheduler::Context::Get(), previous_);

    return *continuation_;
  }

  void Register(Interrupt& interrupt) {
    interrupt_ = &interrupt;
  }

  K_ k_;

  Interrupt* interrupt_ = nullptr;

  Scheduler::Context* previous_ = nullptr;

  using Continuation_ =
      decltype(std::declval<detail::_Reschedule::Composable>()
                   .template k<Arg_>(std::move(k_)));

  std::optional<Continuation_> continuation_;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Preempt {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation {
    Continuation(K_ k, E_ e, std::string name)
      : k_(std::move(k)),
        e_(std::move(e)),
        name_(std::move(name)),
        context_(Scheduler::Default(), &name_) {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        e_(std::move(that.e_)),
        name_(std::move(that.name_)),
        context_(Scheduler::Default(), &name_) {}

    template <typename... Args>
    void Start(Args&&... args) {
      Adapt();

      auto* previous = Scheduler::Context::Switch(&context_);
      CHECK_EQ(previous, previous_);

      eventuals::succeed(*adaptor_, std::forward<Args>(args)...);

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, &context_);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      Adapt();

      auto* previous = Scheduler::Context::Switch(&context_);
      CHECK_EQ(previous, previous_);

      eventuals::fail(*adaptor_, std::forward<Args>(args)...);

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, &context_);
    }

    void Stop() {
      Adapt();

      auto* previous = Scheduler::Context::Switch(&context_);
      CHECK_EQ(previous, previous_);

      eventuals::stop(*adaptor_);

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, &context_);
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
    }

    void Adapt() {
      if (!adaptor_) {
        // Save previous context (even if it's us).
        previous_ = Scheduler::Context::Get();

        adaptor_.emplace(std::move(e_).template k<Arg_>(
            previous_->Reschedule()
                .template k<Value_>(std::move(k_))));

        if (interrupt_ != nullptr) {
          adaptor_->Register(*interrupt_);
        }
      }
    }

    K_ k_;
    E_ e_;
    std::string name_;

    Scheduler::Context context_;

    Interrupt* interrupt_ = nullptr;

    Scheduler::Context* previous_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adaptor_ = decltype(std::declval<E_>().template k<Arg_>(
        (std::declval<Scheduler::Context*>()->Reschedule())
            .template k<Value_>(std::declval<K_>())));

    std::optional<Adaptor_> adaptor_;
  };

  template <typename E_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, E_, Arg>(
          std::move(k),
          std::move(e_),
          std::move(name_));
    }

    E_ e_;
    std::string name_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Preempt(std::string name, E e) {
  return detail::_Preempt::Composable<E>{std::move(e), std::move(name)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
