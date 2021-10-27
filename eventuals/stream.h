#pragma once

// TODO(benh): infinite recursion via thread-local storage.
//
// TODO(benh): 'Stop()' on stream should break infinite recursion
// (figure out how to embed a std::atomic).
//
// TODO(benh): disallow calling 'Next()' after calling 'Done()'.
//
// TODO(benh): disallow calling 'Emit()' before call to 'Next()'.

#include <variant>

#include "eventuals/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct TypeErasedStream {
  virtual ~TypeErasedStream() {}
  virtual void Next() = 0;
  virtual void Done() = 0;
};

////////////////////////////////////////////////////////////////////////

struct _Stream {
  // Helper that distinguishes when a stream's continuation needs to be
  // invoked (versus the stream being invoked as a continuation itself).
  template <typename S_, typename K_, typename Arg_>
  struct StreamK {
    S_* stream_ = nullptr;
    K_* k_ = nullptr;
    std::optional<
        std::conditional_t<
            std::is_void_v<Arg_>,
            std::monostate,
            std::conditional_t<
                std::is_reference_v<Arg_>,
                std::reference_wrapper<std::remove_reference_t<Arg_>>,
                Arg_>>>
        arg_;

    void Start() {
      stream_->previous_->Continue([this]() {
        k_->Start(*stream_);
      });
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      stream_->previous_->Continue(
          [&]() {
            k_->Fail(std::forward<Args>(args)...);
          },
          [&]() {
            // TODO(benh): avoid allocating on heap by storing args in
            // pre-allocated buffer based on composing with Errors.
            auto* tuple = new std::tuple{k_, std::forward<Args>(args)...};
            return [tuple]() {
              std::apply(
                  [](auto* k_, auto&&... args) {
                    k_->Fail(std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
              delete tuple;
            };
          });
    }

    void Stop() {
      stream_->previous_->Continue([this]() {
        k_->Stop();
      });
    }

    template <typename... Args>
    void Emit(Args&&... args) {
      stream_->previous_->Continue(
          [&]() {
            k_->Body(std::forward<Args>(args)...);
          },
          [&]() {
            static_assert(
                sizeof...(args) == 0 || sizeof...(args) == 1,
                "'emit()' only supports 0 or 1 argument, but found > 1");

            static_assert(std::is_void_v<Arg_> || sizeof...(args) == 1);

            if constexpr (!std::is_void_v<Arg_>) {
              arg_.emplace(std::forward<Args>(args)...);
            }

            return [this]() {
              if constexpr (!std::is_void_v<Arg_>) {
                if constexpr (std::is_reference_v<Arg_>) {
                  k_->Body(arg_->get());
                } else {
                  k_->Body(std::move(*arg_));
                }
              } else {
                k_->Body();
              }
            };
          });
    }

    void Ended() {
      stream_->previous_->Continue([this]() {
        k_->Ended();
      });
    }
  };

  template <
      typename K_,
      typename Context_,
      typename Start_,
      typename Next_,
      typename Done_,
      typename Fail_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Continuation : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(
        K_ k,
        Context_ context,
        Start_ start,
        Next_ next,
        Done_ done,
        Fail_ fail,
        Stop_ stop,
        Interrupt_ interrupt)
      : k_(std::move(k)),
        context_(std::move(context)),
        start_(std::move(start)),
        next_(std::move(next)),
        done_(std::move(done)),
        fail_(std::move(fail)),
        stop_(std::move(stop)),
        interrupt_(std::move(interrupt)) {}

    Continuation(Continuation&& that) = default;

    Continuation& operator=(Continuation&& that) {
      // TODO(benh): lambdas don't have an 'operator=()' until C++20 so
      // we have to effectively do a "reset" and "emplace" (as though it
      // was stored in a 'std::optional' but without the overhead of
      // optionals everywhere).
      this->~Continuation();
      new (this) Continuation(std::move(that));

      return *this;
    }

    template <typename... Args>
    void Start(Args&&... args) {
      previous_ = Scheduler::Context::Get();

      streamk_.stream_ = this;
      streamk_.k_ = &k_;

      auto interrupted = [this]() mutable {
        if (handler_) {
          return !handler_->Install();
        } else {
          return false;
        }
      }();

      if (interrupted) {
        assert(handler_);
        handler_->Invoke();
      } else {
        if constexpr (IsUndefined<Start_>::value) {
          streamk_.Start(std::forward<Args>(args)...);
        } else if constexpr (IsUndefined<Context_>::value) {
          start_(streamk_, std::forward<Args>(args)...);
        } else {
          start_(context_, streamk_, std::forward<Args>(args)...);
        }
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      if constexpr (IsUndefined<Fail_>::value) {
        k_.Fail(std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        fail_(k_, std::forward<Args>(args)...);
      } else {
        fail_(context_, k_, std::forward<Args>(args)...);
      }
    }

    void Stop() {
      if constexpr (IsUndefined<Stop_>::value) {
        k_.Stop();
      } else if constexpr (IsUndefined<Context_>::value) {
        stop_(k_);
      } else {
        stop_(context_, k_);
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      if constexpr (!IsUndefined<Interrupt_>::value) {
        handler_.emplace(&interrupt, [this]() {
          if constexpr (IsUndefined<Context_>::value) {
            interrupt_(k_);
          } else {
            interrupt_(context_, k_);
          }
        });
      }
    }

    void Next() override {
      static_assert(
          !IsUndefined<Next_>::value,
          "Undefined 'next' (and no default)");

      previous_->Continue([this]() {
        if constexpr (IsUndefined<Context_>::value) {
          next_(streamk_);
        } else {
          next_(context_, streamk_);
        }
      });
    }

    void Done() override {
      previous_->Continue([this]() {
        if constexpr (IsUndefined<Done_>::value) {
          k_.Ended();
        } else if constexpr (IsUndefined<Context_>::value) {
          done_(streamk_);
        } else {
          done_(context_, streamk_);
        }
      });
    }

    K_ k_;
    Context_ context_;
    Start_ start_;
    Next_ next_;
    Done_ done_;
    Fail_ fail_;
    Stop_ stop_;
    Interrupt_ interrupt_;

    Scheduler::Context* previous_ = nullptr;

    StreamK<Continuation, K_, Value_> streamk_;

    std::optional<Interrupt::Handler> handler_;
  };

  template <
      typename Context_,
      typename Start_,
      typename Next_,
      typename Done_,
      typename Fail_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Builder {
    template <typename Arg>
    using ValueFrom = Value_;

    template <
        typename Value,
        typename... Errors,
        typename Context,
        typename Start,
        typename Next,
        typename Done,
        typename Fail,
        typename Stop,
        typename Interrupt>
    static auto create(
        Context context,
        Start start,
        Next next,
        Done done,
        Fail fail,
        Stop stop,
        Interrupt interrupt) {
      return Builder<
          Context,
          Start,
          Next,
          Done,
          Fail,
          Stop,
          Interrupt,
          Value,
          Errors...>{
          std::move(context),
          std::move(start),
          std::move(next),
          std::move(done),
          std::move(fail),
          std::move(stop),
          std::move(interrupt)};
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<
          K,
          Context_,
          Start_,
          Next_,
          Done_,
          Fail_,
          Stop_,
          Interrupt_,
          Value_,
          Errors_...>(
          std::move(k),
          std::move(context_),
          std::move(start_),
          std::move(next_),
          std::move(done_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Value_, Errors_...>(
          std::move(context),
          std::move(start_),
          std::move(next_),
          std::move(done_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Start>
    auto start(Start start) && {
      static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start),
          std::move(next_),
          std::move(done_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Next>
    auto next(Next next) && {
      static_assert(IsUndefined<Next_>::value, "Duplicate 'next'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(next),
          std::move(done_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Done>
    auto done(Done done) && {
      static_assert(IsUndefined<Done_>::value, "Duplicate 'done'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(next_),
          std::move(done),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Fail>
    auto fail(Fail fail) && {
      static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(next_),
          std::move(done_),
          std::move(fail),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(next_),
          std::move(done_),
          std::move(fail_),
          std::move(stop),
          std::move(interrupt_));
    }

    template <typename Interrupt>
    auto interrupt(Interrupt interrupt) && {
      static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(next_),
          std::move(done_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt));
    }

    Context_ context_;
    Start_ start_;
    Next_ next_;
    Done_ done_;
    Fail_ fail_;
    Stop_ stop_;
    Interrupt_ interrupt_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Stream() {
  return detail::_Stream::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Value,
      Errors...>{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
