#pragma once

#include "stout/undefined.h"

// Helpers for getting invocation result types.

////////////////////////////////////////////////////////////////////////

namespace stout {

////////////////////////////////////////////////////////////////////////

// TODO(benh): Replace with std::type_identity from C++20.
template <typename T>
struct type_identity
{
  using type = T;
};

////////////////////////////////////////////////////////////////////////

template <typename F, typename... Values>
struct InvokeResultPossiblyUndefined
{
  using type = std::invoke_result_t<F, Values...>;
};


template <typename F>
struct InvokeResultPossiblyUndefined<F, Undefined>
{
  using type = typename std::conditional_t<
    std::is_invocable_v<F>,
    std::invoke_result<F>,
    type_identity<Undefined>>::type;
};

////////////////////////////////////////////////////////////////////////

} // namespace stout {

////////////////////////////////////////////////////////////////////////
