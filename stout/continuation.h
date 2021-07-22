#pragma once

#include <type_traits>
#include "stout/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

template<typename K>
struct IsContinuation : std::false_type {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Value>
struct ValueFrom {
    using type = typename K::Value;
};

template<typename Value>
struct ValueFrom<Undefined, Value> {
    using type = Value;
};

////////////////////////////////////////////////////////////////////////

template<typename E>
struct ValuePossiblyUndefined {
    using Value = typename E::Value;
};

template<>
struct ValuePossiblyUndefined<Undefined> {
    using Value = Undefined;
};

////////////////////////////////////////////////////////////////////////

template<typename E, typename K>
struct EKPossiblyUndefined {
    using type = decltype(std::declval<E>() | std::declval<K>());
};

template<typename E>
struct EKPossiblyUndefined<E, Undefined> {
    using type = Undefined;
};

template<typename K>
struct EKPossiblyUndefined<Undefined, K> {
    using type = Undefined;
};

template<>
struct EKPossiblyUndefined<Undefined, Undefined> {
    using type = Undefined;
};

////////////////////////////////////////////////////////////////////////

template<typename K>
struct Compose {
    template<typename Value>
    static auto compose(K k) {
        return std::move(k);
    }
};

////////////////////////////////////////////////////////////////////////

template<typename Value, typename K>
auto compose(K k) {
    return Compose<K>::template compose<Value>(std::move(k));
}

////////////////////////////////////////////////////////////////////////

template<typename K>
struct Unify {
    template<typename Value>
    static auto unify(K k) {
        return std::move(k);
    }
};

////////////////////////////////////////////////////////////////////////

template<typename Value, typename K>
auto unify(K k) {
    return Unify<K>::template unify<Value>(std::move(k));
}

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template<typename E, typename K>
auto operator|(E e, K k) {
    using Value = typename E::Value;
    return std::move(e).k(compose<Value>(std::move(k)));
}

////////////////////////////////////////////////////////////////////////

}   // namespace detail

////////////////////////////////////////////////////////////////////////

}   // namespace eventuals
}   // namespace stout

////////////////////////////////////////////////////////////////////////
