#pragma once

#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Head final {
  template <typename K_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k)
      : k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      stream.Next();
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Body(Arg_ arg) {
      arg_.emplace(std::move(arg));
      CHECK_NOTNULL(stream_)->Done();
    }

    void Ended() {
      if (arg_) {
        k_.Start(std::move(*arg_));
      } else {
        k_.Fail("empty stream");
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    std::optional<Arg_> arg_;

    TypeErasedStream* stream_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>(std::move(k));
    }
  };
};

////////////////////////////////////////////////////////////////////////

inline auto Head() {
  return _Head::Composable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
