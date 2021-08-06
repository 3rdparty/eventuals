#pragma once

#include "stout/eventual.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Reduce {
  template <typename K_>
  struct Adaptor {
    void Start(bool next) {
      if (next) {
        eventuals::next(*stream_);
      } else {
        eventuals::done(*stream_);
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Register(Interrupt&) {
      // Already registered K once in 'Reduce::Register()'.
    }

    K_& k_;
    TypeErasedStream* stream_;
  };

  template <typename K_, typename T_, typename F_, typename Arg_>
  struct Continuation {
    void Start(detail::TypeErasedStream& stream) {
      stream_ = &stream;

      eventuals::next(*stream_);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // TODO(benh): do we need to stop via the adaptor?
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      // TODO(benh): do we need to stop via the adaptor?
      eventuals::stop(k_);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (!adaptor_) {
        adaptor_.emplace(
            f_(static_cast<T_&>(t_))
                .template k<Arg_>(Adaptor<K_>{k_, stream_}));

        if (interrupt_ != nullptr) {
          adaptor_->Register(*interrupt_);
        }
      }

      eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
    }

    void Ended() {
      eventuals::succeed(k_, std::move(t_));
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    T_ t_;
    F_ f_;

    TypeErasedStream* stream_ = nullptr;

    Interrupt* interrupt_ = nullptr;

    using E_ = std::invoke_result_t<F_, std::add_lvalue_reference_t<T_>>;

    static_assert(
        std::is_same_v<bool, typename E_::template ValueFrom<Arg_>>,
        "expecting an eventually returned bool for 'Reduce'");

    using Adaptor_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<Adaptor<K_>>()));

    std::optional<Adaptor_> adaptor_;
  };

  template <typename T_, typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = T_;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, T_, F_, Arg>{
          std::move(k),
          std::move(t_),
          std::move(f_)};
    }

    T_ t_;
    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename T, typename F>
auto Reduce(T t, F f) {
  // static_assert(
  //     !IsContinuation<F>::value
  //     && std::is_invocable_v<F, std::add_lvalue_reference_t<T>>,
  //     "'Reduce' expects a callable in order to provide "
  //     "a reference to the initial accumulator and you can "
  //     "return an eventual from the callable");

  return detail::_Reduce::Composable<T, F>{std::move(t), std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
