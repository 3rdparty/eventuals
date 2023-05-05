#pragma once

#include "eventuals/compose.h"
#include "eventuals/interrupt.h"
#include "eventuals/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Terminal final {
  template <
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_>
  struct Continuation final {
    template <typename... Args>
    void Start(Args&&... args) {
      if constexpr (IsUndefined<Start_>::value) {
        EVENTUALS_LOG(1) << "'Terminal::Start()' reached but undefined";
      } else {
        if constexpr (IsUndefined<Context_>::value) {
          start_(std::forward<Args>(args)...);
        } else {
          start_(context_, std::forward<Args>(args)...);
        }
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (IsUndefined<Fail_>::value) {
        EVENTUALS_LOG(1) << "'Terminal::Fail()' reached but undefined";
      } else {
        if constexpr (IsUndefined<Context_>::value) {
          fail_(std::forward<Error>(error));
        } else {
          fail_(context_, std::forward<Error>(error));
        }
      }
    }

    void Stop() {
      if constexpr (IsUndefined<Stop_>::value) {
        EVENTUALS_LOG(1) << "'Terminal::Stop()' reached but undefined";
      } else {
        if constexpr (IsUndefined<Context_>::value) {
          stop_();
        } else {
          stop_(context_);
        }
      }
    }

    void Register(Interrupt&) {}

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
  };

  template <
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_>
  struct Builder final {
    template <typename...>
    using ValueFrom = void;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <
        typename Context,
        typename Start,
        typename Fail,
        typename Stop>
    static auto create(
        Context context,
        Start start,
        Fail fail,
        Stop stop) {
      return Builder<
          Context,
          Start,
          Fail,
          Stop>{
          std::move(context),
          std::move(start),
          std::move(fail),
          std::move(stop)};
    }

    template <typename Arg, typename Errors, typename... K>
    auto k(K...) && {
      static_assert(
          sizeof...(K) == 0,
          "detected invalid continuation composed _after_ 'Terminal'");

      return Continuation<
          Context_,
          Start_,
          Fail_,
          Stop_>{
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_)};
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create(
          std::move(context),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Start>
    auto start(Start start) && {
      static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
      return create(
          std::move(context_),
          std::move(start),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Fail>
    auto fail(Fail fail) && {
      static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
      return create(
          std::move(context_),
          std::move(start_),
          std::move(fail),
          std::move(stop_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = false;

    using Expects = SingleValue;

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
  };
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Terminal() {
  return _Terminal::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined>{};
}

////////////////////////////////////////////////////////////////////////

struct Stopped final : public Error {
  const char* what() const noexcept override {
    return "Eventual computation stopped (cancelled)";
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
