#pragma once

#include <algorithm>
#include <deque>

#include "eventuals/eventual.h"
#include "eventuals/filter.h"
#include "eventuals/stream.h"
#include "stout/bytes.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _TakeLast final {
  template <typename K_, typename Arg_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, size_t n)
      : n_(n),
        k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (data_.size() == n_) {
        data_.pop_front();
      }
      data_.push_back(std::forward<Args>(args)...);
      stream_->Next();
    }

    // Should be called from the previous eventual (Stream)
    // when the Stream is done, so we are ready to start
    // streaming last values.
    void Ended() {
      // Handle the case where the stream contained fewer than `n_` values.
      n_ = std::min(n_, data_.size());
      ended_ = true;

      auto value = std::move(data_.front());
      data_.pop_front();
      k_.Body(std::move(value));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    void Register(stout::borrowed_ptr<std::pmr::memory_resource>&& resource) {
      k_.Register(std::move(resource));
    }

    void Next() override {
      // When Next is called from the next eventual,
      // the element should be taken from the stored stream.

      // If the stream_ has not produced its values yet,
      // make it do so by calling stream_.Next().
      // When it has produced all its values we'll receive an Ended() call.
      previous_->Continue([this]() {
        if (!ended_) {
          stream_->Next();
        } else if (data_.empty()) {
          // There are no more stored values, our stream has ended.
          k_.Ended();
        } else {
          auto value = std::move(data_.front());
          data_.pop_front();
          k_.Body(std::move(value));
        }
      });
    }

    void Done() override {
      previous_->Continue([this]() {
        k_.Ended();
      });
    }

    Bytes StaticHeapSize() {
      return Bytes(0) + k_.StaticHeapSize();
    }

    size_t n_;

    // NOTE: because we are "taking" we need a value type here (not
    // lvalue or rvalue) and we'll assume that the 'std::forward' will
    // either incur a copy or a move and the compiler should emit the
    // correct errors if those are not allowed.
    std::deque<std::decay_t<Arg_>> data_;

    bool ended_ = false;

    TypeErasedStream* stream_ = nullptr;
    stout::borrowed_ptr<Scheduler::Context> previous_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>(std::move(k), n_);
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;

    size_t n_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _TakeRange final {
  // We've decided to make '_TakeRange' continuation act as a stream.
  // Thus we can avoid situations which might block us for ever telling
  // the stream that 'TakeRange' has everything it wants (in this case
  // rather than 'Loop' calls back up into the stream because 'TakeRange'
  // is in the place, Loop calls back up to 'TakeRange', and then
  // 'TakeRange' decides to continue pulling from the stream or decides
  // that it's all done and then just continue processing).
  template <typename K_, typename Arg_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, size_t begin, size_t amount)
      : begin_(begin),
        amount_(amount),
        k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (begin_ <= i_ && i_ < begin_ + amount_) {
        i_++;
        k_.Body(std::forward<Args>(args)...);
      } else if (i_ < begin_) {
        i_++;
        stream_->Next();
      } else {
        CHECK_EQ(i_, begin_ + amount_);
        stream_->Done();
      }
    }

    void Ended() {
      k_.Ended();
    }

    void Next() override {
      previous_->Continue([this]() {
        if (i_ < begin_ + amount_) {
          stream_->Next();
        } else {
          CHECK_EQ(i_, begin_ + amount_);
          stream_->Done();
        }
      });
    }

    void Done() override {
      previous_->Continue([this]() {
        stream_->Done();
      });
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    void Register(stout::borrowed_ptr<std::pmr::memory_resource>&& resource) {
      k_.Register(std::move(resource));
    }

    Bytes StaticHeapSize() {
      return Bytes(0) + k_.StaticHeapSize();
    }

    size_t begin_;
    size_t amount_;
    size_t i_ = 0;

    TypeErasedStream* stream_ = nullptr;
    stout::borrowed_ptr<Scheduler::Context> previous_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k), begin_, amount_};
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;

    size_t begin_;
    size_t amount_;
  };
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto TakeLast(size_t n) {
  return _TakeLast::Composable{n};
}

[[nodiscard]] inline auto TakeRange(size_t begin, size_t amount) {
  return _TakeRange::Composable{begin, amount};
}

[[nodiscard]] inline auto TakeFirst(size_t amount) {
  return _TakeRange::Composable{0, amount};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
