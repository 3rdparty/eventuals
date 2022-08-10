#pragma once

#include <optional>

#include "eventuals/compose.h"
#include "eventuals/interrupt.h"
#include "eventuals/type-erased-stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Closure final {
  template <typename K_, typename F_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, F_ f)
      : f_(std::move(f)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      continuation().Start(std::forward<Args>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      continuation().Fail(std::forward<Error>(error));
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

    [[nodiscard]] auto& continuation() {
      if (!continuation_) {
        continuation_.emplace(f_().template k<Arg_>(std::move(k_)));

        if (interrupt_ != nullptr) {
          continuation_->Register(*interrupt_);
        }
      }

      return *continuation_;
    }

    F_ f_;

    Interrupt* interrupt_ = nullptr;

    using Continuation_ = decltype(f_().template k<Arg_>(std::declval<K_&&>()));

    std::optional<Continuation_> continuation_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable final {
    using E_ = typename std::invoke_result_t<F_>;

    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = typename E_::template ErrorsFrom<Arg, Errors>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>(std::move(k), std::move(f_));
    }

    // Flags that forbid non-composable things, i.e., a "stream"
    // with an eventual that can not stream or a "loop" with
    // something that is not streaming.
    static constexpr bool Streaming = true;
    static constexpr bool Looping = false;
    static constexpr bool IsEventual = true;

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
