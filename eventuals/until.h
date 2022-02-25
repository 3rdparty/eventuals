#pragma once

#include "eventuals/eventual.h"
#include "eventuals/stream.h"
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Until final {
  template <typename K_, typename Arg_>
  struct Adaptor final {
    void Start(bool done) {
      if (done) {
        stream_.Done();
      } else {
        k_.Body(std::move(arg_));
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::move(error));
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
  struct Adaptor<K_, void> final {
    void Start(bool done) {
      if (done) {
        stream_.Done();
      } else {
        k_.Body();
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::move(error));
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
              std::invoke_result<
                  F_,
                  std::add_lvalue_reference_t<Arg_>>>::type>::value>
  struct Continuation;

  template <typename F_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>(std::move(k), std::move(f_));
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Until::Continuation<K_, F_, Arg_, false> final {
  Continuation(K_ k, F_ f)
    : f_(std::move(f)),
      k_(std::move(k)) {}

  void Begin(TypeErasedStream& stream) {
    stream_ = &stream;

    k_.Begin(stream);
  }

  template <typename Error>
  void Fail(Error&& error) {
    k_.Fail(std::move(error));
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

  F_ f_;

  TypeErasedStream* stream_ = nullptr;

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  K_ k_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Until::Continuation<K_, F_, Arg_, true> final {
  Continuation(K_ k, F_ f)
    : f_(std::move(f)),
      k_(std::move(k)) {}

  void Begin(TypeErasedStream& stream) {
    stream_ = &stream;

    k_.Begin(stream);
  }

  template <typename Error>
  void Fail(Error&& error) {
    k_.Fail(std::move(error));
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
      adapted_.emplace(
          f_(*arg_) // NOTE: passing '&' not '&&'.
              .template k<void>(Adaptor<K_, Arg_>{k_, *arg_, *stream_}));
    } else {
      adapted_.emplace(
          f_().template k<void>(Adaptor<K_, Arg_>{k_, *stream_}));
    }

    if (interrupt_ != nullptr) {
      adapted_->Register(*interrupt_);
    }

    adapted_->Start();
  }

  void Ended() {
    k_.Ended();
  }

  F_ f_;

  TypeErasedStream* stream_ = nullptr;

  Interrupt* interrupt_ = nullptr;

  std::optional<
      std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
      arg_;

  using E_ = typename std::conditional_t<
      std::is_void_v<Arg_>,
      std::invoke_result<F_>,
      std::invoke_result<F_, std::add_lvalue_reference_t<Arg_>>>::type;

  using Adapted_ = decltype(std::declval<E_>().template k<void>(
      std::declval<Adaptor<K_, Arg_>>()));

  std::optional<Adapted_> adapted_;

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  K_ k_;
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Until(F f) {
  return _Until::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
