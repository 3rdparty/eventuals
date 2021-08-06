#pragma once

#include <optional>
#include <string>
#include <tuple>

#include "stout/callback.h"
#include "stout/eventual.h"
#include "stout/interrupt.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Scheduler {
 public:
  struct Context {
    Context(std::string* name)
      : name_(name) {}

    Context(Context&& that) = delete;

    const std::string& name() {
      return *CHECK_NOTNULL(name_);
    }

   private:
    std::string* name_;
  };

  static Scheduler* Default() {
    return default_;
  }

  static void Set(Scheduler* scheduler, Context* context) {
    scheduler_ = scheduler;
    context_ = context;
  }

  static Scheduler* Get(Context** context) {
    assert(scheduler_ != nullptr);
    *context = context_;
    return scheduler_;
  }

  static bool Verify(Scheduler* scheduler, Context* context) {
    return scheduler_ == scheduler && context_ == context;
  }

  static bool Verify(Context* context) {
    return context_ == context;
  }

  virtual void Submit(
      Callback<> callback,
      Context* context,
      bool defer = true) {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    Context* parent = nullptr;
    auto* scheduler = Scheduler::Get(&parent);

    STOUT_EVENTUALS_LOG(1)
        << "'" << context->name() << "' preempting '" << parent->name() << "'";

    Scheduler::Set(this, context);

    callback();

    CHECK(Scheduler::Verify(this, context));

    Scheduler::Set(scheduler, parent);
  }

  // Returns an eventual which will do a 'Submit()' passing the
  // specified context and 'defer = false' in order to continue
  // execution using the execution resource associated with context.
  auto Reschedule(Context* context);

 private:
  static Scheduler* default_;
  static thread_local Scheduler* scheduler_;
  static thread_local Context* context_;
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Reschedule {
  template <typename K_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      static_assert(
          sizeof...(args) == 0 || sizeof...(args) == 1,
          "Reschedule only supports 0 or 1 argument, but found > 1");

      static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

      if constexpr (!std::is_void_v<Arg_>) {
        arg_.emplace(std::forward<Args>(args)...);
      }

      STOUT_EVENTUALS_LOG(1)
          << "Reschedule submitting '" << context_->name() << "'";

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
    void Fail(Args&&... args) {
      // TODO(benh): avoid allocating on heap by storing args in
      // pre-allocated buffer based on composing with Errors.
      auto* tuple = new std::tuple{&k_, std::forward<Args>(args)...};

      scheduler_->Submit(
          [tuple]() {
            std::apply(
                [](auto* k_, auto&&... args) {
                  eventuals::fail(*k_, std::forward<decltype(args)>(args)...);
                },
                std::move(*tuple));
            delete tuple;
          },
          context_,
          /* defer = */ false); // Execute the code immediately if possible.
    }

    void Stop() {
      scheduler_->Submit(
          [this]() {
            eventuals::stop(k_);
          },
          context_,
          /* defer = */ false); // Execute the code immediately if possible.
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
    Scheduler* scheduler_;
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
      return Continuation<K, Arg>{std::move(k), scheduler_, context_};
    }

    Scheduler* scheduler_;
    Scheduler::Context* context_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

inline auto Scheduler::Reschedule(Context* context) {
  return detail::_Reschedule::Composable{this, context};
}

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
        context_(&name_) {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        e_(std::move(that.e_)),
        name_(std::move(that.name_)),
        context_(&name_) {}

    template <typename... Args>
    void Start(Args&&... args) {
      Adapt();

      CHECK(Scheduler::Verify(parent_.scheduler, parent_.context));

      Scheduler::Set(Scheduler::Default(), &context_);

      eventuals::succeed(*adaptor_, std::forward<Args>(args)...);

      CHECK(Scheduler::Verify(Scheduler::Default(), &context_));

      Scheduler::Set(parent_.scheduler, parent_.context);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      Adapt();

      CHECK(Scheduler::Verify(parent_.scheduler, parent_.context));

      Scheduler::Set(Scheduler::Default(), &context_);

      eventuals::fail(*adaptor_, std::forward<Args>(args)...);

      CHECK(Scheduler::Verify(Scheduler::Default(), &context_));

      Scheduler::Set(parent_.scheduler, parent_.context);
    }

    void Stop() {
      Adapt();

      CHECK(Scheduler::Verify(parent_.scheduler, parent_.context));

      Scheduler::Set(Scheduler::Default(), &context_);

      eventuals::stop(*adaptor_);

      CHECK(Scheduler::Verify(Scheduler::Default(), &context_));

      Scheduler::Set(parent_.scheduler, parent_.context);
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
    }

    void Adapt() {
      if (!adaptor_) {
        // Save parent scheduler/context (even if it's us).
        parent_.scheduler = Scheduler::Get(&parent_.context);

        adaptor_.emplace(std::move(e_).template k<Arg_>(
            parent_.scheduler->Reschedule(parent_.context)
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

    struct {
      Scheduler* scheduler = nullptr;
      Scheduler::Context* context = nullptr;
    } parent_;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Reschedule_ = decltype(std::declval<Scheduler>()
                                     .Reschedule(
                                         std::declval<Scheduler::Context*>()));

    using Adaptor_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<Reschedule_>().template k<Value_>(
            std::declval<K_>())));

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
