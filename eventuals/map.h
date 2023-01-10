#pragma once

#include "eventuals/compose.h" // For 'HasValueFrom'.
#include "eventuals/stream.h"
#include "eventuals/then.h"
#include "stout/bytes.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Map final {
  template <typename K_>
  struct Adaptor final {
    template <typename... Args>
    void Start(Args&&... args) {
      k_.Body(std::forward<Args>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt&) {
      // Already registered K once in 'Map::Register()'.
    }

    K_& k_;
  };

  template <typename K_, typename E_, typename Arg_>
  struct Continuation final {
    Continuation(K_ k, E_ e)
      : e_(std::move(e)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      // TODO(benh): do we need to fail via the adaptor?
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      // TODO(benh): do we need to stop via the adaptor?
      k_.Stop();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (!adapted_) {
        adapted_.emplace(std::move(e_).template k<Arg_>(Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }

      adapted_->Start(std::forward<Args>(args)...);
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    Bytes StaticHeapSize() {
      return Bytes(0) + k_.StaticHeapSize();
    }

    E_ e_;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<Adaptor<K_>>()));

    std::optional<Adapted_> adapted_;

    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename>
  struct Traits {
    static constexpr bool exists = false;
  };

  template <typename K_, typename E_, typename Arg_>
  struct Traits<Continuation<K_, E_, Arg_>> {
    static constexpr bool exists = true;
  };

  template <typename E_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        Errors,
        typename E_::template ErrorsFrom<Arg, std::tuple<>>>;

    template <typename Arg, typename K>
    auto k(K k) && {
      // Optimize the case where we compose map on map to lessen the
      // template instantiation load on the compiler.
      //
      // TODO(benh): considering doing this optimization when composing
      // vs here when creating the continuation so that we have a
      // simpler composition graph to lessen the template instantiation
      // load and execution (i.e., graph walk/traversal) at runtime.
      if constexpr (Traits<K>::exists) {
        auto e = std::move(e_) >> std::move(k.e_);
        using E = decltype(e);
        return Continuation<decltype(k.k_), E, Arg>(
            std::move(k.k_),
            std::move(e));
      } else {
        return Continuation<K, E_, Arg>(std::move(k), std::move(e_));
      }
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;

    E_ e_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Map(F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Map' expects a callable (e.g., a lambda) not an eventual");

  auto e = Then(std::move(f));

  using E = decltype(e);

  return _Map::Composable<E>{std::move(e)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
