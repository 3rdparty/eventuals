#pragma once

#include "eventuals/eventual.h"
#include "eventuals/stream.h"
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Returns an eventual which will invoke the specified function when a
// stream/generator begins. Note that the function can return an
// eventual itself but that eventual must eventually return 'void' and
// can not raise any errors.
//
// Example usage:
//
// Iterate({1, 2, 3})
//     >> OnBegin([&]() {
//          // Will only be called once but can be asynchronous!
//          return Timer(std::chrono::milliseconds(10));
//       })
//     >> Collect<std::vector>()
template <typename F>
auto OnBegin(F f);

////////////////////////////////////////////////////////////////////////

struct _OnBegin final {
  template <typename K_>
  struct Adaptor {
    void Start() {
      k_.Begin(stream_);
    }

    template <typename Error>
    void Fail(Error&& error) = delete;

    void Stop() = delete;

    void Register(Interrupt& interrupt) {
      // Already registered K when we created the adaptor.
    }

    K_& k_;
    TypeErasedStream& stream_;
  };

  template <typename K_, typename E_>
  struct Continuation {
    Continuation(K_ k, E_ e)
      : e_(std::move(e)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      adapted_.emplace(
          std::move(e_).template k<void>(Adaptor<K_>{k_, stream}));

      if (interrupt_ != nullptr) {
        adapted_->Register(*interrupt_);
      }

      adapted_->Start();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      k_.Body(std::forward<Args>(args)...);
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
      CHECK_EQ(interrupt_, nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    E_ e_;

    Interrupt* interrupt_ = nullptr;

    using Adapted_ = decltype(std::declval<E_>().template k<void>(
        std::declval<Adaptor<K_>>()));

    std::optional<Adapted_> adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename E_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        Errors,
        typename E_::template ErrorsFrom<void, std::tuple<>>>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, E_>(std::move(k), std::move(e_));
    }

    E_ e_;
  };
};


////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto OnBegin(F f) {
  static_assert(
      std::is_invocable_v<F>,
      "'OnBegin' expects a callable (e.g., a lambda) "
      "that takes no arguments");

  static_assert(
      !HasValueFrom<F>::value,
      "'OnBegin' expects a callable not an eventual");

  auto e = Then(std::move(f));

  using E = decltype(e);

  using Value = typename E::template ValueFrom<void>;

  static_assert(
      std::is_void_v<Value>,
      "'OnBegin' eventual should return 'void'");

  using Errors = typename E::template ErrorsFrom<void, std::tuple<>>;

  static_assert(
      std::is_same_v<Errors, std::tuple<>>,
      "'OnBegin' eventual should not raise any errors");

  return _OnBegin::Composable<E>{std::move(e)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
