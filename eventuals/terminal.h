#pragma once

#include <future>
#include <optional>

#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Terminal {
  template <
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      if constexpr (IsUndefined<Start_>::value) {
        EVENTUALS_LOG(1)
            << "'Terminal::Start()' reached by "
            << Scheduler::Context::Get()->name()
            << " but undefined";
      } else {
        if constexpr (IsUndefined<Context_>::value) {
          start_(std::forward<Args>(args)...);
        } else {
          start_(context_, std::forward<Args>(args)...);
        }
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      if constexpr (IsUndefined<Fail_>::value) {
        EVENTUALS_LOG(1)
            << "'Terminal::Fail()' reached by "
            << Scheduler::Context::Get()->name()
            << " but undefined";
      } else {
        if constexpr (IsUndefined<Context_>::value) {
          if constexpr (sizeof...(args) > 0) {
            fail_(std::forward<Args>(args)...);
          } else {
            fail_(std::runtime_error("empty error"));
          }
        } else {
          if constexpr (sizeof...(args) > 0) {
            fail_(context_, std::forward<Args>(args)...);
          } else {
            fail_(
                context_,
                std::runtime_error("empty error"));
          }
        }
      }
    }

    void Stop() {
      if constexpr (IsUndefined<Stop_>::value) {
        EVENTUALS_LOG(1)
            << "'Terminal::Stop()' reached by "
            << Scheduler::Context::Get()->name()
            << " but undefined";
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
  struct Builder {
    template <typename...>
    using ValueFrom = void;

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

    template <typename Arg, typename... K>
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


    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

inline auto Terminal() {
  return detail::_Terminal::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined>{};
}

////////////////////////////////////////////////////////////////////////

struct StoppedException : public std::exception {
  const char* what() const throw() {
    return "Eventual computation stopped (cancelled)";
  }
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Terminate(E e) {
  using Value = typename E::template ValueFrom<void>;

  std::promise<Value> promise;

  auto future = promise.get_future();

  auto k =
      (std::move(e)
       | eventuals::Terminal()
             .context(std::move(promise))
             .start([](auto& promise, auto&&... values) {
               static_assert(
                   sizeof...(values) == 0 || sizeof...(values) == 1,
                   "Task only supports 0 or 1 value, but found > 1");
               promise.set_value(std::forward<decltype(values)>(values)...);
             })
             .fail([](auto& promise, auto&&... errors) {
               static_assert(
                   sizeof...(errors) == 0 || sizeof...(errors) == 1,
                   "Task only supports 0 or 1 error, but found > 1");
               promise.set_exception(
                   std::make_exception_ptr(
                       std::forward<decltype(errors)>(errors)...));
             })
             .stop([](auto& promise) {
               promise.set_exception(
                   std::make_exception_ptr(
                       StoppedException()));
             }))
          .template k<void>();

  return std::make_tuple(std::move(future), std::move(k));
}

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E>
auto operator*(E e) {
  auto [future, k] = Terminate(std::move(e));

  k.Start();

  return future.get();
}

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Arg, typename E>
auto Build(E e) {
  return std::move(e).template k<Arg>();
}

////////////////////////////////////////////////////////////////////////

template <typename Arg, typename E, typename K>
auto Build(E e, K k) {
  return std::move(e).template k<Arg>(std::move(k));
}

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Build(E e) {
  return std::move(e).template k<void>();
}

////////////////////////////////////////////////////////////////////////

template <typename E, typename K>
auto Build(E e, K k) {
  return std::move(e).template k<void>(std::move(k));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
