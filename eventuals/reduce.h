#pragma once

#include "eventuals/eventual.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Reduce {
  template <typename K_>
  struct Adaptor {
    void Start(bool next) {
      if (next) {
        stream_->Next();
      } else {
        stream_->Done();
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
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
  struct Continuation {
    void Begin(detail::TypeErasedStream& stream) {
      stream_ = &stream;

      stream_->Next();
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // TODO(benh): do we need to stop via the adaptor?
      k_.Fail(std::forward<Args>(args)...);
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

    K_ k_;
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

////////////////////////////////////////////////////////////////////////
