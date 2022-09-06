#pragma once

#include <iostream>
#include <memory>
#ifdef __MACH__
#include <experimental/memory_resource>
#else
#include <memory_resource>
#endif

#include "eventuals/callback.h"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Args>
std::unique_ptr<T, Callback<void(void*)>> MakeUniqueUsingMemoryResourceOrNew(
    stout::borrowed_ptr<std::pmr::memory_resource>& resource,
    Args&&... args) {
  if (resource) {
    size_t size = sizeof(T);
    void* pointer = resource->allocate(size);
    CHECK_NOTNULL(pointer);
    new (pointer) T(std::forward<Args>(args)...);
    return std::unique_ptr<T, Callback<void(void*)>>(
        static_cast<T*>(pointer),
        [resource = resource.reborrow()](void* t) {
          static_cast<T*>(t)->~T();
          resource->deallocate(t, sizeof(T));
        });
  } else {
    return std::unique_ptr<T, Callback<void(void*)>>(
        new T(std::forward<Args>(args)...),
        [](void* t) {
          delete static_cast<T*>(t);
        });
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
