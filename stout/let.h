#pragma once

#include "stout/closure.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

// 'Let()' provides syntactic sugar for using a 'Closure()' when you
// effectively want to introduce a binding that will persist within
// the enclosing scope.
//
// Think of it like a "let binding" that exists in numerous languages,
// for example:
//
// let foo = SomethingThatReturnsAFoo();
//
// You can use 'Let()' anywhere that you would have been able to use a
// callable where you could have returned a 'Closure()' from said
// callable. For example, you can use 'Let()' with 'Then()':
//
// SomethingThatReturnsAFoo()
//     | Then(Let([](auto& foo) {
//         return DoSomethingAsynchronouslyWithFoo(foo)
//             | Then([&]() {
//                  return DoSomethingSynchronouslyWithFoo(foo);
//                });
//       }));
//
// In the above example we need to use 'foo' and rather than
// explicitly doing a 'std::move()' and use a 'Closure()' ourselves we
// can simplify the code by using a 'Let()'.
template <typename F>
auto Let(F f) {
  return [f = std::move(f)](auto value) mutable {
    // NOTE: we need to take 'value' above by value and 'std::move()'
    // it below to capture it otherwise compilers find it hard to
    // instantiate the nested lambda types (see
    // https://stackoverflow.com/q/66617181).
    return Closure([&, value = std::move(value)]() mutable {
      return f(value);
    });
  };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
