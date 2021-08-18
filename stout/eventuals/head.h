#pragma once

#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Head {
  template <typename K_, typename Arg_>
  struct Continuation {
    void Start(TypeErasedStream& stream) {
      stream_ = &stream;
      stream.Next();
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Body(Arg_ arg) {
      arg_.emplace(std::move(arg));
      CHECK_NOTNULL(stream_)->Done();
    }

    void Ended() {
      eventuals::succeed(k_, std::move(*arg_));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;

    std::optional<Arg_> arg_;

    TypeErasedStream* stream_ = nullptr;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k)};
    }
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

inline auto Head() {
  return detail::_Head::Composable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
