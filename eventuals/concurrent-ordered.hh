#pragma once

#include <map>
#include <optional>
#include <tuple>

#include "eventuals/callback.hh"
#include "eventuals/compose.hh"
#include "eventuals/concurrent.hh"
#include "eventuals/flat-map.hh"
#include "eventuals/interrupt.hh"
#include "eventuals/iterate.hh"
#include "eventuals/map.hh"
#include "eventuals/stream.hh"
#include "eventuals/terminal.hh"

/////////////////////////////////////////////////////////////////////

namespace eventuals {

/////////////////////////////////////////////////////////////////////

struct _ReorderAdaptor final {
  template <typename K_, typename Value_>
  struct Continuation final : public TypeErasedStream {
    Continuation(K_ k)
      : k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      upstream_ = &stream;
      k_.Begin(*this);
    }

    // Propagates the received value or
    // store it and 'ask' a next value from a stream.
    template <typename Value>
    void Body(std::tuple<int, std::optional<Value>>&& tuple) {
      CHECK(!done_);
      int i = std::get<0>(tuple);
      if (i < 0) {
        ended_[i * -1] = true;
        Next();
      } else if (index_ == i) {
        CHECK(buffer_[i].empty());
        k_.Body(std::move(std::get<1>(tuple).value()));
      } else {
        CHECK(index_ < i);
        buffer_[i].push_back(std::move(std::get<1>(tuple).value()));
        upstream_->Next();
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    void Stop() {
      k_.Stop();
    }

    void Ended() {
      k_.Ended();
    }

    // Calls 'Next' on 'upstream' in case when there are no stored
    // values, propagate a value from buffer to 'Body' otherwise.
    void Next() override {
      if (!buffer_[index_].empty()) {
        auto value = buffer_[index_].front();
        buffer_[index_].pop_front();
        k_.Body(std::move(value));
      } else if (ended_[index_]) {
        CHECK(buffer_[index_].empty());
        index_++;
        Next();
      } else {
        upstream_->Next();
      }
    }

    void Done() override {
      done_ = true;
      buffer_.clear();
      ended_.clear();
      upstream_->Done();
    }

    TypeErasedStream* upstream_ = nullptr;

    std::map<int, std::deque<Value_>> buffer_;

    int index_ = 1;

    std::map<int, bool> ended_;

    bool done_ = false;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  // Arg there will be received from 'Concurrent::ConcurrentOrderedAdaptor'
  // that equals to 'std::tuple<int, std::optional<Value>>' and we need to
  // extract `Value`.
  struct Composable final {
    template <typename Arg>
    using ValueFrom = typename std::tuple_element<1, Arg>::type::value_type;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<
          K,
          typename std::tuple_element<1, Arg>::type::value_type>(std::move(k));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;
  };
};

/////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto ReorderAdaptor() {
  return _ReorderAdaptor::Composable{};
}

/////////////////////////////////////////////////////////////////////

// Needed to indicate the end of streaming by passing tuple with '-index'.
// Acting like both of 'Stream' and 'Loop'. In case of propagating each value
// as tuple in 'Body' and 'Ended' need to indicate if stream is ended, so it
// saves upstream and on `Next` tries to get a next value from upstream,
// if not ended, otherwise it calls 'Ended'. So 'Next' is both ending and
// getting next values function.
struct _ConcurrentOrderedAdaptor final {
  template <typename K_>
  struct Continuation final : public TypeErasedStream {
    Continuation(K_ k)
      : k_(std::move(k)) {}

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      upstream_ = &stream;
      ended_ = false;
      k_.Begin(*this);
    }

    template <typename Value>
    void Body(std::tuple<int, Value>&& tuple) {
      // NOTE: Either this is the first value we've received on this stream
      // or the index, should be the same as the value we received before.
      int i = std::get<0>(tuple);

      CHECK(!index_ || index_.value() == i);

      index_ = i;
      k_.Body(std::make_tuple(
          i,
          std::make_optional(std::move(std::get<1>(tuple)))));
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    void Stop() {
      k_.Stop();
    }

    void Ended() {
      ended_ = true;
      CHECK(index_);
      k_.Body(std::make_tuple(index_.value() * -1, std::nullopt));
    }

    void Next() override {
      if (ended_) {
        k_.Ended();
      } else {
        upstream_->Next();
      }
    }

    void Done() override {
      upstream_->Done();
    }

    bool ended_ = false;

    std::optional<int> index_;

    TypeErasedStream* upstream_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = std::tuple<
        int,
        std::optional<typename std::tuple_element<1, Arg>::type>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K>(std::move(k));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;
  };
};

/////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto ConcurrentOrderedAdaptor() {
  return _ConcurrentOrderedAdaptor::Composable{};
}

/////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] inline auto ConcurrentOrdered(F f) {
  // NOTE: Starting our index 'i' at 1 because we signal the end of that
  // tranche of values via '-i' which means we can't start at 0.
  return Map([i = 1](auto&& value) mutable {
           return std::make_tuple(i++, std::forward<decltype(value)>(value));
         })
      >> Concurrent([f = std::move(f)]() {
           return FlatMap([&f, j = 1](auto&& tuple) mutable {
             j = std::get<0>(tuple);
             return Iterate({std::move(std::get<1>(tuple))})
                 >> f()
                 >> Map([j](auto&& value) {
                      return std::make_tuple(j, std::move(value));
                    })
                 // A special 'ConcurrentOrderedAdaptor()' allows us to handle
                 // the case when 'f()' has ended so we can propagate down to
                 // 'ReorderAdaptor()' that all elements for the 'i'th tranche
                 // of values has been emitted.
                 >> ConcurrentOrderedAdaptor();
           });
         })
      // Handles the reordering of values by the propagated indexes.
      >> ReorderAdaptor();
}

/////////////////////////////////////////////////////////////////////

} // namespace eventuals

/////////////////////////////////////////////////////////////////////
