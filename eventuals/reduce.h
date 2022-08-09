#pragma once

#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Reduce final {
  template <typename K_>
  struct Adaptor final {
    void Start(bool next) {
      if (next) {
        stream_->Next();
      } else {
        stream_->Done();
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt&) {
      // Already registered K once in 'Reduce::Register()'.
    }

    K_& k_;
    TypeErasedStream* stream_;
  };

  template <typename K_, typename T_, typename F_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, T_ t, F_ f)
      : t_(std::move(t)),
        f_(std::move(f)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;

      stream_->Next();
    }

    template <typename Error>
    void Fail(Error&& error) {
      // TODO(benh): do we need to stop via the adaptor?
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      // TODO(benh): do we need to stop via the adaptor?
      k_.Stop();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (!adapted_) {
        adapted_.emplace(
            f_(static_cast<T_&>(t_))
                .template k<Arg_>(Adaptor<K_>{k_, stream_}));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }

      adapted_->Start(std::forward<Args>(args)...);
    }

    void Ended() {
      k_.Start(std::move(t_));
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    T_ t_;
    F_ f_;

    TypeErasedStream* stream_ = nullptr;

    Interrupt* interrupt_ = nullptr;

    using E_ = std::invoke_result_t<F_, std::add_lvalue_reference_t<T_>>;

    static_assert(
        std::is_same_v<bool, typename E_::template ValueFrom<Arg_>>,
        "expecting an eventually returned bool for 'Reduce'");

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<Adaptor<K_>>()));

    std::optional<Adapted_> adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename T_, typename F_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = T_;

    using E_ =
        std::invoke_result_t<
            F_,
            std::add_lvalue_reference_t<T_>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = typename E_::template ErrorsFrom<Arg, Errors>;

    // Aliases that forbid non-composable things, i.e., a "stream"
    // with an eventual that can not stream or a "loop" with
    // something that is not streaming.
    using Expects = Streaming;
    using Produces = Value;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, T_, F_, Arg>(
          std::move(k),
          std::move(t_),
          std::move(f_));
    }

    T_ t_;
    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename T, typename F>
[[nodiscard]] auto Reduce(T t, F f) {
  static_assert(
      std::is_invocable_v<F, std::add_lvalue_reference_t<T>>,
      "'Reduce' expects a *callable* (e.g., a lambda or functor) "
      "take takes a reference to the accumulator and returns "
      "an eventual");

  using E = std::invoke_result_t<F, std::add_lvalue_reference_t<T>>;

  static_assert(
      HasValueFrom<E>::value,
      "'Reduce' expects a *callable* (e.g., a lambda or functor) "
      "take takes a reference to the accumulator and returns "
      "an eventual");

  return _Reduce::Composable<T, F>{std::move(t), std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
