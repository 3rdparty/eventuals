#pragma once

#include <optional>

#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "stout/bytes.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
////////////////////////////////////////////////////////////////////////

struct _FlatMap final {
  template <typename FlatMap_>
  struct Adaptor final {
    void Begin(TypeErasedStream& stream) {
      CHECK(streamforeach_->adapted_.has_value());
      CHECK(streamforeach_->inner_ == nullptr);
      streamforeach_->inner_ = &stream;
      streamforeach_->inner_->Next();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      streamforeach_->k_.Body(std::forward<Args>(args)...);
    }

    void Ended() {
      CHECK(streamforeach_->adapted_.has_value());
      streamforeach_->adapted_.reset();
      CHECK(streamforeach_->inner_ != nullptr);
      streamforeach_->inner_ = nullptr;

      if (streamforeach_->done_) {
        streamforeach_->outer_->Done();
      } else {
        streamforeach_->outer_->Next();
      }
    }

    void Stop() {
      streamforeach_->Stop();
    }

    template <typename Error>
    void Fail(Error&& error) {
      streamforeach_->Fail(std::forward<Error>(error));
    }

    void Register(Interrupt&) {
      // Already registered K once in 'FlatMap::Body()'.
    }

    void Register(stout::borrowed_ptr<std::pmr::memory_resource>&& resource) {
    }

    FlatMap_* streamforeach_;
  };

  template <typename K_, typename F_, typename Arg_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, F_ f)
      : f_(std::move(f)),
        k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      outer_ = &stream;
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      done_ = true;
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Register(stout::borrowed_ptr<std::pmr::memory_resource>&& resource) {
      resource_ = std::move(resource);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      CHECK(!adapted_.has_value());

      adapted_.emplace(
          f_(std::forward<Args>(args)...)
              .template k<void>(Adaptor<Continuation>{this}));

      if (interrupt_ != nullptr) {
        adapted_->Register(*interrupt_);
      }

      adapted_->Register(std::move(resource_));

      adapted_->Start();
    }

    void Ended() {
      CHECK(!adapted_.has_value());
      k_.Ended();
    }

    void Next() override {
      previous_->Continue([this]() {
        if (adapted_.has_value()) {
          CHECK_NOTNULL(inner_)->Next();
        } else {
          outer_->Next();
        }
      });
    }

    void Done() override {
      previous_->Continue([this]() {
        done_ = true;
        if (adapted_.has_value()) {
          CHECK_NOTNULL(inner_)->Done();
        } else {
          outer_->Done();
        }
      });
    }

    Bytes StaticHeapSize() {
      return Bytes(0) + k_.StaticHeapSize();
    }

    F_ f_;

    TypeErasedStream* outer_ = nullptr;
    TypeErasedStream* inner_ = nullptr;

    using E_ = typename std::invoke_result<F_, Arg_>::type;

    using Adapted_ = decltype(std::declval<E_>().template k<void>(
        std::declval<Adaptor<Continuation>>()));

    std::optional<Adapted_> adapted_;

    Interrupt* interrupt_ = nullptr;

    bool done_ = false;

    stout::borrowed_ptr<Scheduler::Context> previous_;

    stout::borrowed_ptr<std::pmr::memory_resource> resource_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = typename std::conditional_t<
        std::is_void_v<Arg>,
        std::invoke_result<F_>,
        std::invoke_result<F_, Arg>>::type::template ValueFrom<void>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = typename std::conditional_t<
        std::is_void_v<Arg>,
        std::invoke_result<F_>,
        std::invoke_result<F_, Arg>>::type::template ErrorsFrom<void, Errors>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>(std::move(k), std::move(f_));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto FlatMap(F f) {
  return _FlatMap::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
