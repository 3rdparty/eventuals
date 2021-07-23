#pragma once

#include "stout/eventual.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_>
struct MapAdaptor
{
  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::body(k_, std::forward<Args>(args)...);
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
    // Already registered K once in 'Map::Register()'.
  }

  K_& k_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename E_, typename Arg_>
struct Map
{
  void Start(TypeErasedStream& stream)
  {
    eventuals::succeed(k_, stream);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): do we need to fail via the adaptor?
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    // TODO(benh): do we need to stop via the adaptor?
    eventuals::stop(k_);
  }

  template <typename... Args>
  void Body(Args&&... args)
  {
    if (!adaptor_) {
      adaptor_.emplace(std::move(e_).template k<Arg_>(MapAdaptor<K_> { k_ }));

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }
    }

    eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(k_);
  }

  void Register(Interrupt& interrupt)
  {
    assert(interrupt_ == nullptr);
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  K_ k_;
  E_ e_;

  using Adaptor_ = decltype(
      std::declval<E_>().template k<Arg_>(std::declval<MapAdaptor<K_>>()));

  std::optional<Adaptor_> adaptor_;

  Interrupt* interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

template <typename>
struct MapTraits
{
  static constexpr bool exists = false;
};

template <typename K_, typename E_, typename Arg_>
struct MapTraits<Map<K_, E_, Arg_>>
{
  static constexpr bool exists = true;
};

////////////////////////////////////////////////////////////////////////

template <typename E_>
struct MapComposable
{
  template <typename Arg>
  using ValueFrom = typename E_::template ValueFrom<Arg>;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    // Optimize the case where we compose map on map to lessen the
    // template instantiation load on the compiler.
    //
    // TODO(benh): considering doing this optimization when composing
    // vs here when creating the continuation so that we have a
    // simpler composition graph to lessen the template instantiation
    // load and execution (i.e., graph walk/traversal) at runtime.
    if constexpr (MapTraits<K>::exists) {
      auto e = std::move(e_) | std::move(k.e_);
      using E = decltype(e);
      return Map<decltype(k.k_), E, Arg> { std::move(k.k_), std::move(e) };
    } else {
      return Map<K, E_, Arg> { std::move(k), std::move(e_) };
    }
  }

  E_ e_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Map(E e)
{
  return detail::MapComposable<E> { std::move(e) };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
