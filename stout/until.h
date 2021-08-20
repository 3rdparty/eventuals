#pragma once

#include "stout/eventual.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Until {
  template <typename K_, typename Arg_>
  struct Adaptor {
    void Start(bool done) {
      if (done) {
        stream_.Done();
      } else {
        k_.Body(std::move(arg_));
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      // Already registered K once in 'Until::Register()'.
    }

    K_& k_;
    Arg_& arg_;
    TypeErasedStream& stream_;
  };

  template <typename K_>
  struct Adaptor<K_, void> {
    void Start(bool done) {
      if (done) {
        stream_.Done();
      } else {
        k_.Body();
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      // Already registered K once in 'Until::Register()'.
    }

    K_& k_;
    TypeErasedStream& stream_;
  };

  template <typename K_, typename Condition_, typename Arg_>
  struct Continuation {
    void Start(TypeErasedStream& stream) {
      stream_ = &stream;

      k_.Start(stream);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Until' only supports 0 or 1 value");

      if constexpr (!std::is_void_v<Arg_>) {
        arg_.emplace(std::forward<Args>(args)...);
      }

      if (!adaptor_) {
        if constexpr (!std::is_void_v<Arg_>) {
          adaptor_.emplace(
              std::move(condition_)
                  .template k<Arg_>(Adaptor<K_, Arg_>{k_, *arg_, *stream_}));
        } else {
          adaptor_.emplace(
              std::move(condition_)
                  .template k<Arg_>(Adaptor<K_, Arg_>{k_, *stream_}));
        }

        if (interrupt_ != nullptr) {
          adaptor_->Register(*interrupt_);
        }
      }

      if constexpr (!std::is_void_v<Arg_>) {
        adaptor_->Start(*arg_); // NOTE: passing '&' not '&&'.
      } else {
        adaptor_->Start();
      }
    }

    void Ended() {
      k_.Ended();
    }

    K_ k_;
    Condition_ condition_;

    TypeErasedStream* stream_ = nullptr;

    Interrupt* interrupt_ = nullptr;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    using Adaptor_ = decltype(std::declval<Condition_>().template k<Arg_>(
        std::declval<Adaptor<K_, Arg_>>()));

    std::optional<Adaptor_> adaptor_;
  };

  template <typename Condition_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Condition_, Arg>{
          std::move(k),
          std::move(condition_)};
    }

    Condition_ condition_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Condition>
auto Until(Condition condition) {
  return detail::_Until::Composable<Condition>{std::move(condition)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
