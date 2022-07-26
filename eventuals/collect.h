#pragma once

#include <functional>

#include "eventuals/loop.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Collection, typename = void>
struct EventualsCollector {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    // This static_assert will always fail
    // if the compiler needs to compile this implementation.
    static_assert(
        always_false_v<T>,
        "Provide your own EventualsCollector");
  }
};

////////////////////////////////////////////////////////////////////////

// Collectors for STL collections.

template <typename Collection>
struct EventualsCollector<
    Collection,
    std::enable_if_t<
        eventuals::HasEmplaceBack<Collection>::value>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    collection.emplace_back(std::forward<T>(value));
  }
};

template <typename Collection>
struct EventualsCollector<
    Collection,
    std::enable_if_t<
        eventuals::HasInsert<Collection>::value>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    collection.insert(std::forward<T>(value));
  }
};

////////////////////////////////////////////////////////////////////////

struct _Collect final {
  template <typename K_, typename Collection_>
  struct Continuation final {
    Continuation(K_ k)
      : k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      stream_->Next();
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    template <typename Arg>
    void Body(Arg&& value) {
      EventualsCollector<Collection_>::Collect(
          collection_,
          std::forward<Arg>(value));
      stream_->Next();
    }


    void Ended() {
      k_.Start(std::move(collection_));
    }

    void Register(Interrupt& interrupt) {
      CHECK(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    Collection_ collection_ = Collection_();

    TypeErasedStream* stream_ = nullptr;

    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <template <typename...> class Collection_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = Collection_<std::decay_t<Arg>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Collection_<std::decay_t<Arg>>>(std::move(k));
    }
  };

  template <typename Collection_>
  struct ComposableTyped final {
    template <typename Arg>
    using ValueFrom = Collection_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Collection_>(std::move(k));
    }
  };
};

////////////////////////////////////////////////////////////////////////

template <template <typename...> class Collection>
[[nodiscard]] auto Collect() {
  return _Collect::Composable<Collection>();
}

////////////////////////////////////////////////////////////////////////

template <typename Collection>
[[nodiscard]] auto Collect() {
  return _Collect::ComposableTyped<Collection>();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
