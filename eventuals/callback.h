#pragma once

#include <cassert>
#include <type_traits> // For std::aligned_storage.
#include <utility> // For std::move.

#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for using lambdas that only capture 'this' or something less
// than or equal to 'sizeof(void*)' without needing to do any heap
// allocation or use std::function (which increases compile times and
// is not required to avoid heap allocation even if most
// implementations do for small lambdas).
template <typename>
struct Callback;

template <typename R, typename... Args>
struct Callback<R(Args...)> final {
  // TODO(benh): Delete default constructor and force a usage pattern
  // where a delayed initialization requires std::optional so that a
  // user doesn't run into issues where they try and invoke a callback
  // that doesn't go anywhere.
  Callback() = default;

  template <typename F>
  Callback(F f) {
    static_assert(
        !std::is_same_v<Callback, std::decay_t<F>>,
        "Not to be used as a *copy* constructor!");

    this->operator=(std::move(f));
  }

  Callback& operator=(Callback&& that) noexcept {
    if (this == &that) {
      return *this;
    }

    if (base_ != nullptr) {
      base_->~Base();
      base_ = nullptr;
    }

    if (that.base_ != nullptr) {
      base_ = that.base_->Move(&storage_);

      // Set 'base_' to nullptr so we only destruct once.
      that.base_ = nullptr;
    }

    return *this;
  }

  template <typename F>
  Callback& operator=(F f) {
    static_assert(
        !std::is_same_v<Callback, std::decay_t<F>>,
        "Not to be used as a *copy* assignment operator!");

    static_assert(
        sizeof(Handler<F>) <= SIZE,
        "Your callable (e.g., a lambda) is too big for 'Callback' "
        "(do you have too many captures in your lambda?)");

    if (base_ != nullptr) {
      base_->~Base();
    }

    new (&storage_) Handler<F>(std::move(f));

    base_ = reinterpret_cast<Handler<F>*>(&storage_);

    return *this;
  }

  Callback(Callback&& that) noexcept {
    if (that.base_ != nullptr) {
      base_ = that.base_->Move(&storage_);

      // Set 'base_' to nullptr so we only destruct once.
      that.base_ = nullptr;
    }
  }

  ~Callback() {
    if (base_ != nullptr) {
      base_->~Base();
    }
  }

  R operator()(Args... args) {
    assert(base_ != nullptr);
    return base_->Invoke(std::forward<Args>(args)...);
  }

  operator bool() const {
    return base_ != nullptr;
  }

  struct Base {
    virtual ~Base() = default;

    virtual R Invoke(Args... args) = 0;

    virtual Base* Move(void* storage) = 0;
  };

  template <typename F>
  struct Handler final : Base {
    Handler(F f)
      : f_(std::move(f)) {}

    ~Handler() override = default;

    R Invoke(Args... args) override {
      return f_(std::forward<Args>(args)...);
    }

    // TODO(benh): better way to do this?
    Base* Move(void* storage) override {
      new (storage) Handler<F>(std::move(f_));
      return reinterpret_cast<Handler<F>*>(storage);
    }

    F f_;
  };

  // NOTE: helper function used only to get a type so we can determine
  // the size necessary to accomodate storing a
  // 'stout::borrowed_callable'.
  static auto BorrowedCallable() {
    stout::Borrowable<int> borrowable;
    return borrowable.Borrow([i = new int]() {});
  }

  static constexpr std::size_t SIZE =
      sizeof(decltype(BorrowedCallable())) + sizeof(Base);

  std::aligned_storage_t<SIZE> storage_;

  Base* base_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

inline constexpr std::size_t SIZEOF_CALLBACK = Callback<void()>::SIZE;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
