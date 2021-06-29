#pragma once

#include <iostream>

#include "stout/callback.h"
#include "stout/continuation.h"
#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Arg_, typename... Ts_>
struct Adaptor
{
  using Value = typename ValuePossiblyUndefined<K_>::Value;

  template <typename Start>
  Adaptor(K_& k, Ts_... ts, Start start)
    : k_(k), ts_(std::forward<Ts_>(ts)...), start_(std::move(start)) {}

  Adaptor(Adaptor&& that)
    : k_(that.k_), ts_(std::move(that.ts_)), start_(std::move(that.start_)) {}

  K_& k_;
  std::tuple<Ts_...> ts_;
  std::conditional_t<
    !IsUndefined<Arg_>::value,
    Callback<K_&, Ts_..., Arg_&&>,
    Callback<K_&, Ts_...>> start_;

  template <typename... Args>
  void Start(Args&&... args)
  {
    static_assert(
        (IsUndefined<Arg_>::value && sizeof...(args) == 0)
        || sizeof...(args) == 1,
        "Adaptor only supports 0 or 1 value");

    // NOTE: currently we assume that we're passed an rvalue
    // (temporary or '&&') that we can forward on to the callback
    // accepting '&&' or a value (that will get copied).
    static_assert(
        sizeof...(args) == 0
        || !std::is_lvalue_reference_v<
          std::tuple_element<0, std::tuple<decltype(args)...>>>,
        "Detected (currently) unsupported lvalue '&' reference");

    std::apply([&](auto&&... ts) {
      assert(start_);
      start_(k_, std::forward<decltype(ts)>(ts)..., std::forward<Args>(args)...);
    },
    std::move(ts_));
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(k_);
  }

  void Register(Interrupt&)
  {
    // Nothing to do there, think of this like a 'Terminal'.
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg, typename... Ts>
struct IsContinuation<
  detail::Adaptor<K, Arg, Ts...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg, typename... Ts>
struct HasTerminal<
  detail::Adaptor<K, Arg, Ts...>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////

