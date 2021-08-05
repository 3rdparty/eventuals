// TODO(benh): consider moving into 'stout-undefined' repository.

#pragma once

namespace stout {

struct Undefined {};

template <typename>
struct IsUndefined : std::false_type {};

template <>
struct IsUndefined<Undefined> : std::true_type {};

} // namespace stout
