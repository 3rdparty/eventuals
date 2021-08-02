#pragma once

// TODO(benh): infinite recursion via thread-local storage.
//
// TODO(benh): 'stop' on stream should break infinite recursion
// (figure out how to embed a std::atomic).
//
// TODO(benh): disallow calling 'next()' after calling 'done()'.
//
// TODO(benh): disallow calling 'emit()' before call to 'next()'.

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename K, typename... Args>
void emit(K& k, Args&&... args)
{
  k.Emit(std::forward<Args>(args)...);
}

////////////////////////////////////////////////////////////////////////

template <typename K>
void next(K& k)
{
  k.Next();
}

////////////////////////////////////////////////////////////////////////

template <typename K>
void done(K& k)
{
  k.Done();
}

////////////////////////////////////////////////////////////////////////

template <typename K, typename... Args>
void body(K& k, Args&&... args)
{
  k.Body(std::forward<Args>(args)...);
}

////////////////////////////////////////////////////////////////////////

template <typename K>
void ended(K& k)
{
  k.Ended();
}

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct TypeErasedStream
{
  virtual ~TypeErasedStream() {}
  virtual void Next() = 0;
  virtual void Done() = 0;
};

////////////////////////////////////////////////////////////////////////

// Helper that distinguishes when a stream's continuation needs to be
// invoked (versus the stream being invoked as a continuation itself).
template <typename S_, typename K_>
struct StreamK
{
  S_* stream_ = nullptr;
  K_* k_ = nullptr;

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(*k_, *stream_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(*k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(*k_);
  }

  template <typename... Args>
  void Emit(Args&&... args)
  {
    eventuals::body(*k_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(*k_);
  }
};

////////////////////////////////////////////////////////////////////////

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
struct Stream : public TypeErasedStream
{
  // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
  Stream(
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

  Stream(Stream&& that) = default;

  Stream& operator=(Stream&& that)
  {
    // TODO(benh): lambdas don't have an 'operator=()' until C++20 so
    // we have to effectively do a "reset" and "emplace" (as though it
    // was stored in a 'std::optional' but without the overhead of
    // optionals everywhere).
    this->~Stream();
    new(this) Stream(std::move(that));

    return *this;
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
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
        eventuals::start(streamk_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        start_(streamk_, std::forward<Args>(args)...);
      } else {
        start_(context_, streamk_, std::forward<Args>(args)...);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if constexpr (IsUndefined<Fail_>::value) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Args>(args)...);
    } else {
      fail_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Stop()
  {
    if constexpr (!IsUndefined<Stop_>::value) {
      eventuals::stop(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
    }
  }

  void Register(Interrupt& interrupt)
  {
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

  void Next() override
  {
    static_assert(
        !IsUndefined<Next_>::value,
        "Undefined 'next' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      next_(streamk_);
    } else {
      next_(context_, streamk_);
    }
  }

  void Done() override
  {
    if constexpr (IsUndefined<Done_>::value) {
      eventuals::ended(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      done_(streamk_);
    } else {
      done_(context_, streamk_);
    }
  }

  K_ k_;
  Context_ context_;
  Start_ start_;
  Next_ next_;
  Done_ done_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  StreamK<Stream, K_> streamk_;

  std::optional<Interrupt::Handler> handler_;
};

////////////////////////////////////////////////////////////////////////

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
struct StreamBuilder
{
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
      Interrupt interrupt)
  {
    return StreamBuilder<
      Context,
      Start,
      Next,
      Done,
      Fail,
      Stop,
      Interrupt,
      Value,
      Errors...> {
      std::move(context),
      std::move(start),
      std::move(next),
      std::move(done),
      std::move(fail),
      std::move(stop),
      std::move(interrupt)
    };
  }

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    return Stream<
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
  auto context(Context context) &&
  {
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
  auto start(Start start) &&
  {
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
  auto next(Next next) &&
  {
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
  auto done(Done done) &&
  {
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
  auto fail(Fail fail) &&
  {
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
  auto stop(Stop stop) &&
  {
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
  auto interrupt(Interrupt interrupt) &&
  {
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

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Stream()
{
  return detail::StreamBuilder<
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Value,
    Errors...> {};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
