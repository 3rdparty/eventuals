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
        eventuals::done(stream_);
      } else {
        eventuals::body(k_, std::move(arg_));
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
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
        eventuals::done(stream_);
      } else {
        eventuals::body(k_);
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Register(Interrupt& interrupt) {
      // Already registered K once in 'Until::Register()'.
    }

    K_& k_;
    TypeErasedStream& stream_;
  };

  template <typename K_, typename Condition_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(TypeErasedStream& stream, Args&&... args) {
      stream_ = &stream;

      eventuals::succeed(k_, stream, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
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
        eventuals::succeed(*adaptor_, *arg_); // NOTE: passing '&' not '&&'.
      } else {
        eventuals::succeed(*adaptor_);
      }
    }

    void Ended() {
      eventuals::ended(k_);
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
