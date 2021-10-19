#pragma once

#include "stout/eventual.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Filter {
  template <typename K_, typename F_, typename Arg_>
  struct Continuation {
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

    K_ k_;
    F_ f_;

    TypeErasedStream* stream_ = nullptr;
  };

  // Using at compose.h to correctly create custom Continuation structure
  // with provided lambda function to filter
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

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Filter(F f) {
  return detail::_Filter::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
