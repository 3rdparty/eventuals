#pragma once

#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Range {
  template <typename K_, typename Arg_>
  struct Continuation : public detail::TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, int from, int to, int step)
      : k_(std::move(k)),
        from_(from),
        to_(to),
        step_(step) {}

    void Start() {
      previous_ = Scheduler::Context::Get();

      k_.Start(*this);
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

    void Ended() {
      k_.Ended();
    }

    void Next() override {
      if (from_ == to_
          || step_ == 0
          || (from_ > to_ && step_ > 0)
          || (from_ < to_ && step_ < 0)) {
        k_.Ended();
      } else {
        previous_->Continue([this]() {
          int temp = from_;
          from_ += step_;
          k_.Body(temp);
        });
      }
    }

    void Done() override {
      previous_->Continue([this]() {
        k_.Ended();
      });
    }

    K_ k_;
    int from_;
    const int to_;
    const int step_;

    Scheduler::Context* previous_ = nullptr;
  };

  struct Composable {
    template <typename>
    using ValueFrom = int;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k), from_, to_, step_};
    }

    const int from_;
    const int to_;
    const int step_;
  };
};

inline auto Range(int from, int to, int step) {
  return _Range::Composable{from, to, step};
}

inline auto Range(int from, int to) {
  return Range(from, to, 1);
}

inline auto Range(int to) {
  return Range(0, to, 1);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
