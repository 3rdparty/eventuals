#pragma once

#include "eventuals/eventual.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Map {
  template <typename K_>
  struct Adaptor {
    template <typename... Args>
    void Start(Args&&... args) {
      k_.Body(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
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
  struct Continuation {
    void Start(TypeErasedStream& stream) {
      k_.Start(stream);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // TODO(benh): do we need to fail via the adaptor?
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      // TODO(benh): do we need to stop via the adaptor?
      k_.Stop();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (!adaptor_) {
        adaptor_.emplace(std::move(e_).template k<Arg_>(Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          adaptor_->Register(*interrupt_);
        }
      }

      adaptor_->Start(std::forward<Args>(args)...);
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    E_ e_;

    using Adaptor_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<Adaptor<K_>>()));

    std::optional<Adaptor_> adaptor_;

    Interrupt* interrupt_ = nullptr;
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
  struct Composable {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

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
        auto e = std::move(e_) | std::move(k.e_);
        using E = decltype(e);
        return Continuation<decltype(k.k_), E, Arg>{
            std::move(k.k_),
            std::move(e)};
      } else {
        return Continuation<K, E_, Arg>{std::move(k), std::move(e_)};
      }
    }

    E_ e_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Map(E e) {
  return detail::_Map::Composable<E>{std::move(e)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
