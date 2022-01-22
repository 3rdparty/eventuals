#pragma once

#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include "eventuals/callback.h"
#include "eventuals/closure.h"
#include "eventuals/compose.h"
#include "eventuals/interrupt.h"
#include "eventuals/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Scheduler {
 public:
  struct Context {
    virtual ~Context() {}

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

    Context(Scheduler* scheduler)
      : scheduler_(scheduler) {}

    Context(Context&& that) = delete;

    virtual const std::string& name() = 0;

    template <typename F>
    void Unblock(F f) {
      scheduler_->Submit(std::move(f), this);
    }

    template <typename F>
    void Continue(F&& f) {
      if (scheduler_->Continuable(this)) {
        auto* previous = Switch(this);
        f();
        Switch(previous);
      } else {
        scheduler_->Submit(std::move(f), this);
      }
    }

    template <typename F, typename G>
    void Continue(F&& f, G&& g) {
      if (scheduler_->Continuable(this)) {
        auto* previous = Switch(this);
        f();
        Switch(previous);
      } else {
        scheduler_->Submit(g(), this);
      }
    }

    auto* scheduler() {
      return scheduler_;
    }

   private:
    static thread_local Context* current_;

    Scheduler* scheduler_;
  };

  static Scheduler* Default();

  virtual bool Continuable(Context* context) = 0;

  virtual void Submit(Callback<> callback, Context* context) = 0;
};

////////////////////////////////////////////////////////////////////////

struct _Reschedule {
  template <typename K_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      context_->Continue(
          [&]() {
            k_.Start(std::forward<Args>(args)...);
          },
          [&]() {
            static_assert(
                sizeof...(args) == 0 || sizeof...(args) == 1,
                "Reschedule only supports 0 or 1 argument, but found > 1");

            static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

            if constexpr (!std::is_void_v<Arg_>) {
              arg_.emplace(std::forward<Args>(args)...);
            }

            EVENTUALS_LOG(1)
                << "Reschedule submitting '" << context_->name() << "'";

            return [this]() {
              if constexpr (sizeof...(args) == 1) {
                k_.Start(std::move(*arg_));
              } else {
                k_.Start();
              }
            };
          });
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      context_->Continue(
          [&]() {
            k_.Fail(std::forward<Args>(args)...);
          },
          [&]() {
            // TODO(benh): avoid allocating on heap by storing args in
            // pre-allocated buffer based on composing with Errors.
            using Tuple = std::tuple<decltype(&k_), Args...>;
            auto tuple = std::make_unique<Tuple>(
                &k_,
                std::forward<Args>(args)...);
            return [tuple = std::move(tuple)]() mutable {
              std::apply(
                  [](auto* k_, auto&&... args) {
                    k_->Fail(std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
            };
          });
    }

    void Stop() {
      context_->Continue([this]() {
        k_.Stop();
      });
    }

    void Begin(TypeErasedStream& stream) {
      CHECK(stream_ == nullptr);
      stream_ = &stream;

      context_->Continue(
          [&]() {
            k_.Begin(*CHECK_NOTNULL(stream_));
          },
          [&]() {
            EVENTUALS_LOG(1)
                << "Reschedule submitting '" << context_->name() << "'";

            return [this]() {
              k_.Begin(*CHECK_NOTNULL(stream_));
            };
          });
    }

    template <typename... Args>
    void Body(Args&&... args) {
      context_->Continue(
          [&]() {
            k_.Body(std::forward<Args>(args)...);
          },
          [&]() {
            static_assert(
                sizeof...(args) == 0 || sizeof...(args) == 1,
                "Reschedule only supports 0 or 1 argument, but found > 1");

            static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

            if constexpr (!std::is_void_v<Arg_>) {
              arg_.emplace(std::forward<Args>(args)...);
            }

            EVENTUALS_LOG(1)
                << "Reschedule submitting '" << context_->name() << "'";

            return [this]() {
              if constexpr (sizeof...(args) == 1) {
                k_.Body(std::move(*arg_));
              } else {
                k_.Body();
              }
            };
          });
    }

    void Ended() {
      context_->Continue([this]() {
        k_.Ended();
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

    TypeErasedStream* stream_ = nullptr;
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

// Returns an eventual which will switch to the specified context
// before continuing it's continuation.
inline auto Reschedule(Scheduler::Context* context) {
  return _Reschedule::Composable{context};
}

////////////////////////////////////////////////////////////////////////

// Returns an eventual which will ensure that after the specified
// eventual 'e' has completed the scheduler context used before 'e'
// will be used to reschedule the next continuation.
template <typename E>
auto RescheduleAfter(E e) {
  return Closure([e = std::move(e)]() mutable {
    Scheduler::Context* previous = Scheduler::Context::Get();
    return std::move(e)
        | Reschedule(previous);
  });
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
          Reschedule(previous_).template k<Arg_>(std::move(k_)));

      if (interrupt_ != nullptr) {
        continuation_->Register(*interrupt_);
      }
    }

    // NOTE: there is no invariant that 'previous_' equals the current
    // context, i.e., 'Scheduler::Context::Get()' in cases when the
    // continuation has already been emplaced. For example, this may
    // occur when a different thread/context is triggering an
    // interrupt.

    return *continuation_;
  }

  void Register(Interrupt& interrupt) {
    interrupt_ = &interrupt;
  }

  K_ k_;

  Interrupt* interrupt_ = nullptr;

  Scheduler::Context* previous_ = nullptr;

  using Continuation_ =
      decltype(std::declval<_Reschedule::Composable>()
                   .template k<Arg_>(std::move(k_)));

  std::optional<Continuation_> continuation_;
};

////////////////////////////////////////////////////////////////////////

struct _Preempt {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation : public Scheduler::Context {
    Continuation(K_ k, E_ e, std::string name)
      : Scheduler::Context(Scheduler::Default()),
        k_(std::move(k)),
        e_(std::move(e)),
        name_(std::move(name)) {}

    Continuation(Continuation&& that)
      : Scheduler::Context(Scheduler::Default()),
        k_(std::move(that.k_)),
        e_(std::move(that.e_)),
        name_(std::move(that.name_)) {}

    const std::string& name() override {
      return name_;
    }

    template <typename... Args>
    void Start(Args&&... args) {
      Adapt();

      auto* previous = Scheduler::Context::Switch(this);
      CHECK_EQ(previous, previous_);

      adapted_->Start(std::forward<Args>(args)...);

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, this);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      Adapt();

      auto* previous = Scheduler::Context::Switch(this);
      CHECK_EQ(previous, previous_);

      adapted_->Fail(std::forward<Args>(args)...);

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, this);
    }

    void Stop() {
      Adapt();

      auto* previous = Scheduler::Context::Switch(this);
      CHECK_EQ(previous, previous_);

      adapted_->Stop();

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, this);
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
    }

    void Adapt() {
      if (!adapted_) {
        // Save previous context (even if it's us).
        previous_ = Scheduler::Context::Get();

        adapted_.emplace(std::move(e_).template k<Arg_>(
            Reschedule(previous_).template k<Value_>(std::move(k_))));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }
    }

    K_ k_;
    E_ e_;
    std::string name_;

    Interrupt* interrupt_ = nullptr;

    Scheduler::Context* previous_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<_Reschedule::Composable>()
            .template k<Value_>(std::declval<K_>())));

    std::optional<Adapted_> adapted_;
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

template <typename E>
auto Preempt(std::string name, E e) {
  return _Preempt::Composable<E>{std::move(e), std::move(name)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
