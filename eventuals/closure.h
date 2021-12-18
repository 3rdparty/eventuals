#pragma once

#include <optional>

#include "eventuals/compose.h"
#include "eventuals/interrupt.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Forward declaration (to break circular dependency).
struct TypeErasedStream;

////////////////////////////////////////////////////////////////////////

struct _Closure {
  template <typename K_, typename F_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      continuation().Start(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      continuation().Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      continuation().Stop();
    }

    void Begin(TypeErasedStream& stream) {
      continuation().Begin(stream);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      continuation().Body(std::forward<Args>(args)...);
    }

    void Ended() {
      continuation().Ended();
    }

    void Register(Interrupt& interrupt) {
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
    }

    auto& continuation() {
      if (!continuation_) {
        continuation_.emplace(f_().template k<Arg_>(std::move(k_)));

        if (interrupt_ != nullptr) {
          continuation_->Register(*interrupt_);
        }
      }

      return *continuation_;
    }

    K_ k_;
    F_ f_;

    Interrupt* interrupt_ = nullptr;

    using Continuation_ = decltype(f_().template k<Arg_>(std::declval<K_&&>()));

    std::optional<Continuation_> continuation_;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom =
        typename std::invoke_result_t<F_>::template ValueFrom<Arg>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>{std::move(k), std::move(f_)};
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Closure(F f) {
  static_assert(
      std::is_invocable_v<F>,
      "'Closure' expects a *callable* (e.g., a lambda or functor) "
      "that doesn't expect any arguments");

  using E = decltype(f());

  static_assert(
      HasValueFrom<E>::value,
      "Expecting an eventual continuation as the "
      "result of the callable passed to 'Closure'");

  return _Closure::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
