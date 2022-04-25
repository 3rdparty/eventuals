#pragma once

#include <functional> // For 'std::reference_wrapper'.
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include "eventuals/callback.h"
#include "eventuals/closure.h"
#include "eventuals/compose.h"
#include "eventuals/interrupt.h"
#include "eventuals/lazy.h"
#include "eventuals/terminal.h"
#include "eventuals/undefined.h"
#include "stout/stringify.hpp"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Scheduler {
 public:
  struct Context final {
    static Context* Get() {
      return CHECK_NOTNULL(current_);
    }

    static Context* Switch(Context* context) {
      Context* previous = current_;
      current_ = CHECK_NOTNULL(context);
      return CHECK_NOTNULL(previous);
    }

    Context(Scheduler* scheduler, std::string&& name, void* data = nullptr)
      : data(data),
        scheduler_(CHECK_NOTNULL(scheduler)),
        name_(std::move(name)) {}

    Context(std::string&& name)
      : Context(Context::Get()->scheduler(), std::move(name)) {
      scheduler()->Clone(this);
    }

    Context(const Context& that) = delete;

    Context(Context&& that) = delete;

    ~Context() = default;

    Scheduler* scheduler() {
      return CHECK_NOTNULL(scheduler_);
    }

    void block() {
      blocked_ = true;
    }

    void unblock() {
      blocked_ = false;
    }

    bool blocked() {
      return blocked_;
    }

    const std::string& name() {
      return name_;
    }

    template <typename F>
    void Unblock(F f) {
      scheduler()->Submit(std::move(f), this);
    }

    template <typename F>
    void Continue(F&& f) {
      if (scheduler()->Continuable(this)) {
        auto* previous = Switch(this);
        f();
        Switch(previous);
      } else {
        scheduler()->Submit(std::move(f), this);
      }
    }

    template <typename F, typename G>
    void Continue(F&& f, G&& g) {
      if (scheduler()->Continuable(this)) {
        auto* previous = Switch(this);
        f();
        Switch(previous);
      } else {
        scheduler()->Submit(g(), this);
      }
    }

    // Schedulers that need arbitrary data for this context can use 'data'.
    void* data = nullptr;

    // Many schedulers need to store a blocked context in some kind of data
    // structure and you can use 'next' for doing that.
    Context* next = nullptr;

    // Schedulers can use 'callback' to store the function that "starts" or
    // "unblocks"/"resumes" the context.
    Callback<void()> callback;

   private:
    static thread_local Context* current_;

    Scheduler* scheduler_ = nullptr;

    // There is the most common set of variables to create contexts.
    bool blocked_ = false;

    std::string name_;
  };

  virtual ~Scheduler() = default;

  static Scheduler* Default();

  virtual bool Continuable(Context* context) = 0;

  virtual void Submit(Callback<void()> callback, Context* context) = 0;

  virtual void Clone(Context* child) = 0;
};

////////////////////////////////////////////////////////////////////////

struct _Reschedule final {
  template <typename K_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, Scheduler::Context* context)
      : context_(context),
        k_(std::move(k)) {}

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

    template <typename Error>
    void Fail(Error&& error) {
      context_->Continue(
          [&]() {
            k_.Fail(std::forward<Error>(error));
          },
          [&]() {
            // TODO(benh): avoid allocating on heap by storing args in
            // pre-allocated buffer based on composing with Errors.
            using Tuple = std::tuple<decltype(&k_), Error>;
            auto tuple = std::make_unique<Tuple>(
                &k_,
                std::forward<Error>(error));

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

    Scheduler::Context* context_;

    std::optional<
        std::conditional_t<
            !std::is_void_v<Arg_>,
            std::conditional_t<
                std::is_reference_v<Arg_>,
                std::reference_wrapper<std::remove_reference_t<Arg_>>,
                Arg_>,
            Undefined>>
        arg_;

    TypeErasedStream* stream_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>(std::move(k), context_);
    }

    Scheduler::Context* context_;
  };
};

////////////////////////////////////////////////////////////////////////

// Returns an eventual which will switch to the specified context
// before continuing it's continuation.
[[nodiscard]] inline auto Reschedule(Scheduler::Context* context) {
  return _Reschedule::Composable{context};
}

////////////////////////////////////////////////////////////////////////

// Returns an eventual which will ensure that after the specified
// eventual 'e' has completed the scheduler context used before 'e'
// will be used to reschedule the next continuation.
template <typename E>
[[nodiscard]] auto RescheduleAfter(E e) {
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
struct Reschedulable final {
  Reschedulable(K_ k)
    : k_(std::move(k)) {}

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

  Interrupt* interrupt_ = nullptr;

  Scheduler::Context* previous_ = nullptr;

  using Continuation_ =
      decltype(std::declval<_Reschedule::Composable>()
                   .template k<Arg_>(std::declval<K_>()));

  std::optional<Continuation_> continuation_;

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  K_ k_;
};

////////////////////////////////////////////////////////////////////////

struct _Preempt final {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, E_ e, std::string name)
      : context_(
          Scheduler::Default(),
          std::move(name)),
        e_(std::move(e)),
        k_(std::move(k)) {}

    Continuation(Continuation&& that)
      : context_(
          Scheduler::Default(),
          std::move(const_cast<std::string&>(that.context_.name()))),
        e_(std::move(that.e_)),
        k_(std::move(that.k_)) {
      CHECK_EQ(that.previous_, nullptr) << "moving after starting";
    }

    ~Continuation() = default;

    template <typename... Args>
    void Start(Args&&... args) {
      Adapt();

      auto* previous = Scheduler::Context::Switch(&context_);
      CHECK_EQ(previous, previous_);

      adapted_->Start(std::forward<Args>(args)...);

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, &context_);
    }

    template <typename Error>
    void Fail(Error&& error) {
      Adapt();

      auto* previous = Scheduler::Context::Switch(&context_);
      CHECK_EQ(previous, previous_);

      adapted_->Fail(std::forward<Error>(error));

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, &context_);
    }

    void Stop() {
      Adapt();

      auto* previous = Scheduler::Context::Switch(&context_);
      CHECK_EQ(previous, previous_);

      adapted_->Stop();

      auto* context = Scheduler::Context::Switch(previous_);
      CHECK_EQ(context, &context_);
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

    Scheduler::Context context_;

    E_ e_;

    Interrupt* interrupt_ = nullptr;

    Scheduler::Context* previous_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<_Reschedule::Composable>()
            .template k<Value_>(std::declval<K_>())));

    std::optional<Adapted_> adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename E_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = typename E_::template ErrorsFrom<Arg, Errors>;

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
[[nodiscard]] auto Preempt(std::string name, E e) {
  return _Preempt::Composable<E>{std::move(e), std::move(name)};
}

////////////////////////////////////////////////////////////////////////

// Helper that "promisifies" an eventual, i.e., builds and returns a
// continuation 'k' that you can start along with a 'std::future' that
// you can use to wait for the eventual value.
//
// NOTE: uses the default, i.e., preemptive, scheduler so that the
// eventual has it's own 'Scheduler::Context'.
template <typename E>
[[nodiscard]] auto Promisify(std::string&& name, E e) {
  using Value = typename E::template ValueFrom<void>;

  std::promise<
      typename ReferenceWrapperTypeExtractor<Value>::type>
      promise;

  auto future = promise.get_future();

  auto k = Build(
      Closure([context = Lazy<Scheduler::Context>(
                   Scheduler::Default(),
                   std::move(name))]() mutable {
        // NOTE: intentionally rescheduling with our context and never
        // rescheduling again because when we terminate we're done!
        return Reschedule(context.get());
      })
      | std::move(e)
      | Terminal()
            .context(std::move(promise))
            .start([](auto& promise, auto&&... values) {
              static_assert(
                  sizeof...(values) == 0 || sizeof...(values) == 1,
                  "'Promisify()' only supports 0 or 1 value, but found > 1");
              promise.set_value(std::forward<decltype(values)>(values)...);
            })
            .fail([](auto& promise, auto&& error) {
              promise.set_exception(
                  make_exception_ptr_or_forward(
                      std::forward<decltype(error)>(error)));
            })
            .stop([](auto& promise) {
              promise.set_exception(
                  std::make_exception_ptr(
                      StoppedException()));
            }));

  return std::make_tuple(std::move(future), std::move(k));
}

////////////////////////////////////////////////////////////////////////

// Overload of the dereference operator for eventuals.
//
// NOTE: THIS IS BLOCKING! CONSIDER YOURSELF WARNED!
template <typename E, std::enable_if_t<HasValueFrom<E>::value, int> = 0>
auto operator*(E e) {
  try {
    auto [future, k] = Promisify(
        // NOTE: using the current thread id in order to constuct a task
        // name because the thread blocks so this name should be unique!
        "[thread "
            + stringify(std::this_thread::get_id())
            + " blocking on dereference]",
        std::move(e));

    k.Start();

    return future.get();
  } catch (const std::exception& e) {
    LOG(WARNING)
        << "WARNING: exception thrown while dereferencing eventual: "
        << e.what();
    throw;
  } catch (...) {
    LOG(WARNING)
        << "WARNING: exception thrown while dereferencing eventual";
    throw;
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
