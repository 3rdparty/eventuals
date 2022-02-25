#pragma once

#include <algorithm>
#include <deque>

#include "eventuals/eventual.h"
#include "eventuals/filter.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _TakeLastN final {
  template <typename K_, typename Arg_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, size_t n)
      : n_(n),
        k_(std::move(k)) {}

    Continuation(Continuation&& that) = default;

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::move(error));
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

    size_t n_;

    // NOTE: because we are "taking" we need a value type here (not
    // lvalue or rvalue) and we'll assume that the 'std::forward' will
    // either incur a copy or a move and the compiler should emit the
    // correct errors if those are not allowed.
    std::deque<std::decay_t<Arg_>> data_;

    bool ended_ = false;

    TypeErasedStream* stream_ = nullptr;
    Scheduler::Context* previous_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>(std::move(k), std::move(n_));
    }

    size_t n_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _TakeRange final {
  template <typename K_, typename Arg_>
  struct Continuation final {
    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;
      k_.Begin(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::move(error));
    }

    void Stop() {
      k_.Stop();
    }

    template <typename... Args>
    // 'in_range_' needs to prevent calling Next
    // when stream has already passed the set amount_ of elements.
    void Body(Args&&... args) {
      if (CheckRange()) {
        in_range_ = true;
        k_.Body(std::forward<Args>(args)...);
      } else if (!in_range_) {
        stream_->Next();
      } else {
        stream_->Done();
      }
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    bool CheckRange() {
      bool result = i_ >= begin_ && i_ < begin_ + amount_;
      ++i_;
      return result;
    }

    K_ k_;
    size_t begin_;
    size_t amount_;
    size_t i_ = 0;
    bool in_range_ = false;

    TypeErasedStream* stream_ = nullptr;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k), begin_, amount_};
    }

    size_t begin_;
    size_t amount_;
  };
};

////////////////////////////////////////////////////////////////////////

inline auto TakeLastN(size_t N) {
  return _TakeLastN::Composable{N};
}

inline auto TakeRange(size_t begin, size_t amount) {
  return _TakeRange::Composable{begin, amount};
}

inline auto TakeFirstN(size_t amount) {
  return _TakeRange::Composable{0, amount};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
