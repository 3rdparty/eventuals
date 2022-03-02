#pragma once

#include "eventuals/eventual.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Filter final {
  template <typename K_, typename F_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, F_ f)
      : f_(std::move(f)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      k_.Begin(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (f_(std::forward<Args>(args)...)) {
        k_.Body(std::forward<Args>(args)...);
      } else {
        stream_->Next();
      }
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    F_ f_;

    TypeErasedStream* stream_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  // Using at compose.h to correctly create custom Continuation structure
  // with provided lambda function to filter
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

template <typename F>
auto Filter(F f) {
  return _Filter::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
