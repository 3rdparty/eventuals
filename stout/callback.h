#pragma once

#include <type_traits>   // For std::aligned_storage.

////////////////////////////////////////////////////////////////////////

namespace stout {

////////////////////////////////////////////////////////////////////////

// Helper for using lambdas that only capture 'this' or something less
// than or equal to 'sizeof(void*)' without needing to do any heap
// allocation or use std::function (which increases compile times and
// is not required to avoid heap allocation even if most
// implementations do for small lambdas).

template<typename... Args>
struct Callback {
    // TODO(benh): Delete default constructor and force a usage pattern
    // where a delayed initialization requires std::optional so that a
    // user doesn't run into issues where they try and invoke a callback
    // that doesn't go anywhere.
    Callback() {}

    template<typename F>
    Callback(F f) {
        static_assert(!std::is_same_v<Callback, std::decay_t<F>>,
                      "Not to be used as a *copy* constructor!");

        this->operator=(std::move(f));
    }

    Callback& operator=(Callback&& that) {
        if (base_ != nullptr) {
            base_->~Base();
            base_ = nullptr;
        }

        if (that.base_ != nullptr) {
            base_      = that.base_->Move(&storage_);

            // Set 'base_' to nullptr so we only destruct once.
            that.base_ = nullptr;
        }

        return *this;
    }

    template<typename F>
    Callback& operator=(F f) {
        static_assert(!std::is_same_v<Callback, std::decay_t<F>>,
                      "Not to be used as a *copy* assignment operator!");

        static_assert(sizeof(Handler<F>) <= SIZE);
        if (base_ != nullptr) { base_->~Base(); }
        new (&storage_) Handler<F>(std::move(f));
        base_ = reinterpret_cast<Handler<F>*>(&storage_);
        return *this;
    }

    Callback(Callback&& that) {
        if (that.base_ != nullptr) {
            base_      = that.base_->Move(&storage_);

            // Set 'base_' to nullptr so we only destruct once.
            that.base_ = nullptr;
        }
    }

    ~Callback() {
        if (base_ != nullptr) { base_->~Base(); }
    }

    void operator()(Args... args) {
        assert(base_ != nullptr);
        base_->Invoke(std::forward<Args>(args)...);
    }

    operator bool() const { return base_ != nullptr; }

    struct Base {
        virtual ~Base()                    = default;

        virtual void  Invoke(Args... args) = 0;

        virtual Base* Move(void* storage)  = 0;
    };

    template<typename F>
    struct Handler : Base {
        Handler(F f) : f_(std::move(f)) {}

        virtual ~Handler() = default;

        void Invoke(Args... args) override { f_(std::forward<Args>(args)...); }

        // TODO(benh): better way to do this?
        Base* Move(void* storage) override {
            new (storage) Handler<F>(std::move(f_));
            return reinterpret_cast<Handler<F>*>(storage);
        }

        F f_;
    };

    static constexpr std::size_t SIZE = sizeof(void*) + sizeof(Base);

    std::aligned_storage_t<SIZE> storage_;

    Base*                        base_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

}   // namespace stout

////////////////////////////////////////////////////////////////////////
