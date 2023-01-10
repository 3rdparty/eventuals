#pragma once

#include "eventuals/stream.h"
#include "stout/bytes.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Range final {
  template <typename K_, typename Arg_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, int from, int to, int step)
      : from_(from),
        to_(to),
        step_(step),
        k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    ~Continuation() override = default;

    void Start() {
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
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

    Bytes StaticHeapSize() {
      return Bytes(0) + k_.StaticHeapSize();
    }

    int from_;
    const int to_;
    const int step_;

    stout::borrowed_ptr<Scheduler::Context> previous_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename>
    using ValueFrom = int;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>(std::move(k), from_, to_, step_);
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = SingleValue;

    const int from_;
    const int to_;
    const int step_;
  };
};

[[nodiscard]] inline auto Range(int from, int to, int step) {
  return _Range::Composable{from, to, step};
}

[[nodiscard]] inline auto Range(int from, int to) {
  return Range(from, to, 1);
}

[[nodiscard]] inline auto Range(int to) {
  return Range(0, to, 1);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
