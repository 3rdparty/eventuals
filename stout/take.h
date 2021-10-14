#pragma once

#include <algorithm>
#include <deque>

#include "stout/eventual.h"
#include "stout/filter.h"
#include "stout/stream.h"

namespace stout {
namespace eventuals {
namespace detail {

////////////////////////////////////////////////////////////////////////

struct _TakeLastN {
  template <typename K_, typename Arg_>
  struct Continuation : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, size_t n)
      : k_(std::move(k)),
        n_(n) {}

    void Start(TypeErasedStream& stream) {
      stream_ = &stream;
      previous_ = Scheduler::Context::Get();

      k_.Start(*this);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
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

    void Next() override {
      // When Next is called from the next eventual,
      // the element should be taken from the stored stream.

      // If the stream_ has not produced its values yet,
      // make it do so by calling stream_.Next().
      // When it has produced all its values we'll receive an Ended() call.
      previous_->Continue([this]() {
        if (!ended_) {
          stream_->Next();
          return;
        }
        if (data_.empty()) {
          // There are no more stored values, our stream has ended.
          k_.Ended();
          return;
        }
        auto value = std::move(data_.front());
        data_.pop_front();
        k_.Body(std::move(value));
      });
    }

    void Done() override {
      previous_->Continue([this]() {
        k_.Ended();
      });
    }

    K_ k_;
    size_t n_;
    std::deque<Arg_> data_;
    bool ended_ = false;

    TypeErasedStream* stream_ = nullptr;
    Scheduler::Context* previous_ = nullptr;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k), std::move(n_)};
    }

    size_t n_;
  };
};

////////////////////////////////////////////////////////////////////////

struct _TakeRange {
  template <typename K_, typename Arg_>
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

  struct Composable {
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

} // namespace detail

////////////////////////////////////////////////////////////////////////

inline auto TakeLastN(size_t N) {
  return detail::_TakeLastN::Composable{N};
}

inline auto TakeRange(size_t begin, size_t amount) {
  return detail::_TakeRange::Composable{begin, amount};
}

inline auto TakeFirstN(size_t amount) {
  return detail::_TakeRange::Composable{0, amount};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
