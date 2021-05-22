#pragma once

namespace stout {

////////////////////////////////////////////////////////////////////////

// Helper for using lambdas that only capture 'this' or something less
// than or equal to 'sizeof(void*)' without needing to do any heap
// allocation or use std::function (which increases compile times and
// is not required to avoid heap allocation even if most
// implementations do for small lambdas).

template <typename... Args>
struct Callback
{
  Callback() {}

  template <typename F>
  Callback(F f)
  {
    this->operator=(std::move(f));
  }

  template <typename F>
  Callback& operator=(F f)
  {
    static_assert(sizeof(Handler<F>) <= SIZE);
    new(&storage_) Handler<F>(std::move(f));
    base_ = reinterpret_cast<Handler<F>*>(&storage_);
    return *this;
  }

  Callback(Callback&& that)
    : storage_(std::move(that.storage_)),
      base_(that.base_)
  {
    // Set 'base_' to nullptr so 'Destruct()' is only invoked once.
    that.base_ = nullptr;
  }

  ~Callback()
  {
    // TODO(benh): better way to do this?
    if (base_ != nullptr) {
      base_->Destruct();
    }
  }

  void operator()(Args... args)
  {
    assert(base_ != nullptr);
    base_->Invoke(std::forward<Args>(args)...);
  }

  operator bool() const
  {
    return base_ != nullptr;
  }

  struct Base
  {
    virtual ~Base() = default;

    virtual void Invoke(Args... args) = 0;

    // TODO(benh): better way to do this?
    virtual void Destruct() = 0;
  };

  template <typename F>
  struct Handler : Base
  {
    Handler(F f) : f_(std::move(f)) {}

    void Invoke(Args... args) override
    {
      f_(std::forward<Args>(args)...);
    }

    // TODO(benh): better way to do this?
    void Destruct() override { f_.~F(); }

    F f_;
  };

  static constexpr std::size_t SIZE = sizeof(void*) + sizeof(Base);

  std::aligned_storage_t<SIZE> storage_;

  Base* base_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace stout {
