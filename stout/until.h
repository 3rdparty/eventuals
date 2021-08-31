#pragma once

#include "stout/eventual.h"
#include "stout/stream.h"
#include "stout/then.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Until {
  template <typename K_, typename Arg_>
  struct Adaptor {
    void Start(bool done) {
      if (done) {
        stream_.Done();
      } else {
        k_.Body(std::move(arg_));
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      // Already registered K once in 'Until::Register()'.
    }

    K_& k_;
    Arg_& arg_;
    TypeErasedStream& stream_;
  };

  template <typename K_>
  struct Adaptor<K_, void> {
    void Start(bool done) {
      if (done) {
        stream_.Done();
      } else {
        k_.Body();
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      // Already registered K once in 'Until::Register()'.
    }

    K_& k_;
    TypeErasedStream& stream_;
  };

  template <
      typename K_,
      typename F_,
      typename Arg_,
      bool = HasValueFrom<
          typename std::conditional_t<
              std::is_void_v<Arg_>,
              std::invoke_result<F_>,
              std::invoke_result<F_, std::add_lvalue_reference_t<Arg_>>>::type,
          void>::value>
  struct Continuation;

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>{std::move(k), std::move(f_)};
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Until::Continuation<K_, F_, Arg_, false> {
  void Start(TypeErasedStream& stream) {
    stream_ = &stream;

    k_.Start(stream);
  }

  template <typename... Args>
  void Fail(Args&&... args) {
    k_.Fail(std::forward<Args>(args)...);
  }

  void Stop() {
    k_.Stop();
  }

  void Register(Interrupt& interrupt) {
    k_.Register(interrupt);
  }

  template <typename... Args>
  void Body(Args&&... args) {
    bool done = f_(args...); // NOTE: explicitly not forwarding.
    if (done) {
      stream_->Done();
    } else {
      k_.Body(std::forward<Args>(args)...); // NOTE: forward here.
    }
  }

  void Ended() {
    k_.Ended();
  }

  K_ k_;
  F_ f_;

  TypeErasedStream* stream_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Until::Continuation<K_, F_, Arg_, true> {
  using E_ = typename std::conditional_t<
      std::is_void_v<Arg_>,
      std::invoke_result<F_>,
      std::invoke_result<F_, std::add_lvalue_reference_t<Arg_>>>::type;

  void Start(TypeErasedStream& stream) {
    stream_ = &stream;

    k_.Start(stream);
  }

  template <typename... Args>
  void Fail(Args&&... args) {
    k_.Fail(std::forward<Args>(args)...);
  }

  void Stop() {
    k_.Stop();
  }

  void Register(Interrupt& interrupt) {
    assert(interrupt_ == nullptr);
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  template <typename... Args>
  void Body(Args&&... args) {
    static_assert(
        !std::is_void_v<Arg_> || sizeof...(args) == 0,
        "'Until' only supports 0 or 1 value");

    if constexpr (!std::is_void_v<Arg_>) {
      arg_.emplace(std::forward<Args>(args)...);
    }

    if constexpr (!std::is_void_v<Arg_>) {
      adaptor_.emplace(
          f_(*arg_) // NOTE: passing '&' not '&&'.
              .template k<void>(Adaptor<K_, Arg_>{k_, *arg_, *stream_}));
    } else {
      adaptor_.emplace(
          f_()
              .template k<void>(Adaptor<K_, Arg_>{k_, *stream_}));
    }

    if (interrupt_ != nullptr) {
      adaptor_->Register(*interrupt_);
    }

    adaptor_->Start();
  }

  void Ended() {
    k_.Ended();
  }

  K_ k_;
  F_ f_;

  TypeErasedStream* stream_ = nullptr;

  Interrupt* interrupt_ = nullptr;

  std::optional<
      std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
      arg_;

  using Adaptor_ = decltype(std::declval<E_>().template k<void>(
      std::declval<Adaptor<K_, Arg_>>()));

  std::optional<Adaptor_> adaptor_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Until(F f) {
  return detail::_Until::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
