#pragma once

#include <optional>
#include <string>

#include "stout/eventual.h"
#include "stout/callback.h"
#include "stout/interrupt.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

class Scheduler
{
public:
  struct Context
  {
    Context(std::string* name) : name_(name) {}

    const std::string& name() { return *CHECK_NOTNULL(name_); }

  private:
    std::string* name_;
  };

  static Scheduler* Default()
  {
    return default_;
  }

  static void Set(Scheduler* scheduler, Context* context)
  {
    scheduler_ = scheduler;
    context_ = context;
  }

  static Scheduler* Get(Context** context)
  {
    assert(scheduler_ != nullptr);
    *context = context_;
    return scheduler_;
  }

  static bool Verify(Scheduler* scheduler, Context* context)
  {
    return scheduler_ == scheduler && context_ == context;
  }

  static bool Verify(Context* context)
  {
    return context_ == context;
  }

  virtual void Submit(Callback<> callback, Context* context, bool defer = true)
  {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    Context* parent = nullptr;
    auto* scheduler = Scheduler::Get(&parent);
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

template <typename K_, typename Arg_>
struct Reschedule
{
  template <typename... Args>
  void Start(Args&&... args)
  {
    static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
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
  void Fail(Args&&... args)
  {
    // TODO(benh): avoid allocating on heap by storing args in
    // pre-allocated buffer based on composing with Errors.
    auto* tuple = new std::tuple { &k_, std::forward<Args>(args)... };

    scheduler_->Submit(
        [tuple]() {
          std::apply([](auto* k_, auto&&... args) {
            eventuals::fail(*k_, std::forward<decltype(args)>(args)...);
          },
          std::move(*tuple));
          delete tuple;
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
  Scheduler::Context* context_;

  std::optional<
    std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>> arg_;
};

////////////////////////////////////////////////////////////////////////

struct RescheduleComposable
{
  template <typename Arg>
  using ValueFrom = Arg;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
   return Reschedule<K, Arg> { std::move(k), scheduler_, context_ };
  }

  Scheduler* scheduler_;
  Scheduler::Context* context_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

inline auto Scheduler::Reschedule(Context* context)
{
  return detail::RescheduleComposable { this, context };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
