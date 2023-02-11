#pragma once

#include "eventuals/compose.h" // For 'HasValueFrom'.
#include "eventuals/eventual.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Repeat final {
  template <typename K_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k)
      : k_(std::move(k)) {}

    Continuation(Continuation&& that) = default;

    ~Continuation() override {
      CHECK(!unwindable_ && !unwind_next_ && !unwind_done_);
    }

    template <typename... Args>
    void Start(Args&&...) {
      previous_ = Reborrow(Scheduler::Context::Get());

      unwindable_ = true;

      k_.Begin(*this);

      while (unwind_next_ || unwind_done_) {
        if (unwind_next_) {
          CHECK(!unwind_done_);
          unwind_next_ = false;
          previous_->Continue([this]() {
            k_.Body();
          });
        } else {
          CHECK(unwind_done_);
          unwind_done_ = false;
          previous_->Continue([this]() {
            k_.Ended();
          });
          CHECK(!unwind_next_ && !unwind_done_);
        }
      }

      unwindable_ = false;
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    void Next() override {
      if (!unwindable_) {
        unwindable_ = true;
        unwind_next_ = true;
        while (unwind_next_ || unwind_done_) {
          if (unwind_next_) {
            CHECK(!unwind_done_);
            unwind_next_ = false;
            previous_->Continue([this]() {
              k_.Body();
            });
          } else {
            CHECK(unwind_done_);
            unwind_done_ = false;
            previous_->Continue([this]() {
              k_.Ended();
            });
            CHECK(!unwind_next_ && !unwind_done_);
          }
        }
        unwindable_ = false;
      } else {
        CHECK(!unwind_next_ && !unwind_done_);
        unwind_next_ = true;
        DLOG(INFO) << "UNWINDING NEXT";
      }
    }

    void Done() override {
      if (!unwindable_) {
        previous_->Continue([this]() {
          k_.Ended();
        });
      } else {
        CHECK(!unwind_next_ && !unwind_done_);
        unwind_done_ = true;
        DLOG(INFO) << "UNWINDING DONE";
      }
    }

    stout::borrowed_ptr<Scheduler::Context> previous_;

    bool unwindable_ = false;
    bool unwind_next_ = false;
    bool unwind_done_ = false;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = void;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) {
      return Continuation<K>(std::move(k));
    }
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Repeat(F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Repeat' expects a callable (e.g., a lambda) not an eventual");

  return _Repeat::Composable{} | Map(std::move(f));
}

[[nodiscard]] inline auto Repeat() {
  return _Repeat::Composable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
