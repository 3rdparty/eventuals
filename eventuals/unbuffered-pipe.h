#pragma once

#include <optional>

#include "eventuals/closure.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/on-begin.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Provides an unbuffered "pipe" which ensures that readers/writers
// rendezvous with one another.
//
// NOTE: multiple simulatneous readers and writers are permitted!
template <typename T>
class UnbufferedPipe final : public Synchronizable {
 public:
  UnbufferedPipe()
    : has_value_or_closed_(&lock()),
      become_writer_or_closed_(&lock()),
      has_reader_or_closed_(&lock()),
      become_reader_or_closed_(&lock()),
      has_writer_or_closed_(&lock()) {}

  ~UnbufferedPipe() override {
    // TODO(benh): check we're closed and have no more borrowers!
  }

  [[nodiscard]] auto Write(T&& value) {
    return Synchronized(
        Then([this, value = std::move(value)]() mutable {
          return WaitToBeWriterOrClosed()
              >> WaitForReaderOrClosed()
              >> Then([&]() {
                   // TODO(benh): raise an error if already closed?
                   if (!is_closed_) {
                     value_.emplace(std::move(value));
                     has_value_or_closed_.Notify();

                     writer_ = false;
                     become_writer_or_closed_.Notify();
                   }
                 });
        }));
  }

  [[nodiscard]] auto Read() {
    return Repeat()
        >> Synchronized(
               Map([this]() {
                 return WaitToBeReaderOrClosed()
                     >> WaitForValueOrClosed()
                     >> Then([this]() {
                          std::optional<T> value;
                          if (!is_closed_) {
                            CHECK(value_);
                            value.emplace(std::move(value_.value()));
                            value_.reset();
                          }
                          reader_ = false;
                          become_reader_or_closed_.Notify();
                          return value;
                        });
               }))
        >> Until([](auto& value) {
             return !value.has_value();
           })
        >> Map([](auto&& value) {
             CHECK(value);
             // NOTE: need to use 'Just' here in case 'T' is an
             // eventual otherwise we'll try and compose with it here!
             return Just(std::move(*value));
           });
  }

  // NOTE: this implementation does not allow any other writers once
  // we start plumbing which is nice because there won't be any weird
  // interleaving.
  [[nodiscard]] auto Plumb() {
    return Closure([this, done = false]() mutable {
      return Synchronized(
                 Map([&](auto&& value) {
                   static_assert(
                       std::is_convertible_v<std::decay_t<decltype(value)>, T>,
                       "'UnbufferedPipe' redirection expects a type "
                       "convertible to the type of the 'UnbufferedPipe'");

                   CHECK(writer_);
                   CHECK(reader_);
                   // TODO(benh): raise an error if already closed or
                   // if closed before value was read?
                   value_.emplace(std::move(value));
                   has_value_or_closed_.Notify();
                 }))
          // NOTE: need to release the lock so a reader can consume
          // the value! However, we remain the writer so that values
          // don't get interleaved from 'Write()'.
          >> Synchronized(
                 Map([&]() {
                   return WaitForReaderOrClosed()
                       >> Then([&]() {
                            done = is_closed_;
                          });
                 })
                 >> OnBegin([&]() {
                     return WaitToBeWriterOrClosed()
                         >> WaitForReaderOrClosed()
                         >> Then([&]() {
                              done = is_closed_;
                            });
                   }))
          >> Loop()
                 .begin([&done](auto& stream) {
                   if (!done) {
                     stream.Next();
                   } else {
                     stream.Done();
                   }
                 })
                 .body([&done](auto& stream) {
                   if (!done) {
                     stream.Next();
                   } else {
                     stream.Done();
                   }
                 });
    });
  }

  [[nodiscard]] auto Close() {
    return Synchronized(Then([this]() {
      // TODO(benh): raise an error if already closed?
      is_closed_ = true;
      has_value_or_closed_.NotifyAll();
      become_writer_or_closed_.NotifyAll();
      has_reader_or_closed_.NotifyAll();
      become_reader_or_closed_.NotifyAll();
      has_writer_or_closed_.NotifyAll();
    }));
  }

 private:
  [[nodiscard]] auto WaitToBeWriterOrClosed() {
    CHECK(lock().OwnedByCurrentSchedulerContext());
    return Then([this]() {
             return become_writer_or_closed_.Wait([this]() {
               return writer_ && !is_closed_;
             });
           })
        >> Then([this]() {
             if (!is_closed_) {
               CHECK(!writer_);
               writer_ = true;
               has_writer_or_closed_.Notify();
             }
           });
  }

  [[nodiscard]] auto WaitToBeReaderOrClosed() {
    CHECK(lock().OwnedByCurrentSchedulerContext());
    return Then([this]() {
             return become_reader_or_closed_.Wait([this]() {
               return reader_ && !is_closed_;
             });
           })
        >> Then([this]() {
             if (!is_closed_) {
               CHECK(!reader_);
               reader_ = true;
               has_reader_or_closed_.Notify();
             }
           });
  }

  [[nodiscard]] auto WaitForWriterOrClosed() {
    CHECK(lock().OwnedByCurrentSchedulerContext());
    return has_writer_or_closed_.Wait([this]() {
      return !writer_ && !is_closed_;
    });
  }

  [[nodiscard]] auto WaitForReaderOrClosed() {
    CHECK(lock().OwnedByCurrentSchedulerContext());
    return has_reader_or_closed_.Wait([this]() {
      return !reader_ && !is_closed_;
    });
  }

  [[nodiscard]] auto WaitForValueOrClosed() {
    CHECK(lock().OwnedByCurrentSchedulerContext());
    return has_value_or_closed_.Wait([this]() {
      return !value_.has_value() && !is_closed_;
    });
  }

  ConditionVariable has_value_or_closed_;
  ConditionVariable become_writer_or_closed_;
  ConditionVariable has_reader_or_closed_;
  ConditionVariable become_reader_or_closed_;
  ConditionVariable has_writer_or_closed_;
  std::optional<T> value_;
  bool reader_ = false;
  bool writer_ = false;
  bool is_closed_ = false;
};

////////////////////////////////////////////////////////////////////////

template <typename T>
[[nodiscard]] auto Plumb(UnbufferedPipe<T>& pipe) {
  return pipe.Plumb();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
