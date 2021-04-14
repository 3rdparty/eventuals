#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/eventuals.h"
#include "stout/streams.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::continuation;
using stout::eventuals::ended;
using stout::eventuals::eventual;
using stout::eventuals::loop;
using stout::eventuals::map;
using stout::eventuals::reduce;
using stout::eventuals::stream;
using stout::eventuals::terminal;
using stout::eventuals::transform;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;


TEST(EventualTest, Succeed)
{
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto i = eventual<int>(
      5,
      [](auto& context, auto& k) {
        auto thread = std::thread(
            [&context, &k]() mutable {
              succeed(k, context);
            });
        thread.detach();
      },
      [&](auto&, auto&) {
        stop.Call();
      });

  auto j = i
    | [](int i) { return i + 2; }
    | continuation<int>(
        9,
        [](auto& context, auto& k, auto&& value) {
          auto thread = std::thread(
              [value, &context, &k]() mutable {
                succeed(k, context - value);
              });
          thread.detach();
        },
        [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_EQ(2, eventuals::run(eventuals::task(j)));
}


TEST(EventualTest, Fail)
{
  // Using mocks to ensure start and stop callback don't get invoked.
  MockFunction<void()> start, stop;

  EXPECT_CALL(start, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto i = eventual<int>(
      5,
      [](auto& context, auto& k) {
        auto thread = std::thread(
            [&context, &k]() mutable {
              fail(k, context);
            });
        thread.detach();
      },
      [&](auto&, auto&) {
        stop.Call();
      });

  auto j = i
    | [](int i) { return i + 2; }
    | continuation<int>(
        [&](auto&, auto&, auto&&) {
          start.Call();
        },
        [](auto&, auto& k, auto&& error) {
          fail(k, std::forward<decltype(error)>(error));
        },
        [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_THROW(eventuals::run(eventuals::task(j)), FailedException);
}


TEST(EventualTest, Stopped)
{
  // Using mocks to ensure start is only called once and fail
  // callbacks don't get invoked.
  MockFunction<void()> start, fail;

  EXPECT_CALL(start, Call())
    .Times(1);

  EXPECT_CALL(fail, Call())
    .Times(0);

  auto i = eventual<int>(
      5,
      [&](auto&, auto& k) {
        start.Call();
      },
      [](auto&, auto& k) {
        stop(k);
      });

  auto j = i
    | [](int i) { return i + 2; }
    | continuation<int>(
        [&](auto&, auto&, auto&&) {
          start.Call();
        },
        [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        [](auto&, auto& k) {
          stop(k);
        });

  auto t = eventuals::task(j);

  eventuals::start(t);

  eventuals::stop(t);

  EXPECT_THROW(eventuals::wait(t), StoppedException);
}


TEST(EventualTest, Reuse)
{
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto operation = [&](int i, auto&& promise) {
    return eventual<int>(
        i,
        [](auto& context, auto& k) {
          auto thread = std::thread(
              [&context, &k]() mutable {
                succeed(k, context);
              });
          thread.detach();
        },
        [&](auto&, auto&) {
          stop.Call();
        })
      | [](int i) { return i + 2; }
      | continuation<int>(
          9,
          [](auto& context, auto& k, auto&& value) {
            auto thread = std::thread(
                [value, &context, &k]() mutable {
                  succeed(k, context - value);
                });
            thread.detach();
          },
          [&](auto&, auto&, auto&&) {
            fail.Call();
          },
          [&](auto&, auto&) {
            stop.Call();
          })
      | terminal(
          std::move(promise),
          [](auto& promise, auto&& value) {
            promise.set_value(std::forward<decltype(value)>(value));
          },
          [](auto& promise, auto&& error) {
            promise.set_exception(std::make_exception_ptr(FailedException()));
          },
          [](auto& promise) {
            promise.set_exception(std::make_exception_ptr(StoppedException()));
          });
  };

  using Operation = decltype(operation(int(), std::promise<int>()));

  std::promise<int> promise1;

  auto future = promise1.get_future();

  auto* o = new Operation(operation(5, std::move(promise1)));

  eventuals::start(*o);

  EXPECT_EQ(2, future.get());

  std::promise<int> promise2;

  future = promise2.get_future();

  *o = operation(4, std::move(promise2));

  eventuals::start(*o);

  EXPECT_EQ(3, future.get());

  delete o;
}


TEST(StreamTest, Succeed)
{
    // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop, done;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  EXPECT_CALL(done, Call())
    .Times(0);

  auto s = stream<int>(
      5,
      /* next = */ [](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      },
      /* done = */ [&](auto&, auto&) {
        done.Call();
      },
      /* stop = */ [&](auto&, auto&) {
        stop.Call();
      })
    | loop<int>(
        0,
        /* start = */ [](auto&, auto& stream) {
          next(stream);
        },
        /* body = */ [](auto& sum, auto& stream, auto&& value) {
          sum += value;
          next(stream);
        },
        /* ended = */ [](auto& sum, auto& k) {
          succeed(k, sum);
        },
        /* fail = */ [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        /* stop = */ [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_EQ(15, eventuals::run(eventuals::task(s)));
}


TEST(StreamTest, Done)
{
    // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto s = stream<int>(
      0,
      /* next = */ [](auto&, auto& k) {
        emit(k, 0);
      },
      /* done = */ [](auto&, auto& k) {
        ended(k);
      },
      /* stop = */ [&](auto&, auto&) {
        stop.Call();
      })
    | loop<int>(
        0,
        /* body = */ [](auto& count, auto& stream, auto&&) {
          if (++count == 2) {
            done(stream);
          } else {
            next(stream);
          }
        },
        /* ended = */ [](auto& count, auto& k) {
          succeed(k, count);
        },
        /* fail = */ [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        /* stop = */ [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_EQ(2, eventuals::run(eventuals::task(s)));
}


TEST(StreamTest, Fail)
{
    // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> stop, done, fail, ended;

  EXPECT_CALL(stop, Call())
    .Times(0);

  EXPECT_CALL(done, Call())
    .Times(0);

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(ended, Call())
    .Times(0);

  auto s = stream<int>(
      0,
      /* next = */ [](auto&, auto& k) {
        eventuals::fail(k, 0);
      },
      /* done = */ [&](auto&, auto&) {
        done.Call();
      },
      /* stop = */ [&](auto&, auto&) {
        stop.Call();
      })
    | loop<int>(
        0,
        /* body = */ [](auto&, auto& stream, auto&&) {
          next(stream);
        },
        /* ended = */ [&](auto&, auto&) {
          ended.Call();
        },
        /* fail = */ [&](auto&, auto& k, auto&& error) {
          eventuals::fail(k, std::forward<decltype(error)>(error));
        },
        /* stop = */ [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_THROW(eventuals::run(eventuals::task(s)), FailedException);
}


TEST(StreamTest, Stop)
{
    // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> body, done, fail, ended;

  EXPECT_CALL(body, Call())
    .Times(1);

  EXPECT_CALL(done, Call())
    .Times(0);

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(ended, Call())
    .Times(0);

  auto s = stream<int>(
      0,
      /* next = */ [](auto&, auto& k) {
        emit(k, 0);
      },
      /* done = */ [&](auto&, auto&) {
        done.Call();
      },
      /* stop = */ [](auto&, auto& k) {
        stop(k);
      })
    | loop<int>(
        0,
        /* body = */ [&](auto&, auto&, auto&&) {
          body.Call();
        },
        /* ended = */ [&](auto&, auto&) {
          ended.Call();
        },
        /* fail = */ [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        /* stop = */ [](auto&, auto& k) {
          stop(k);
        });

  auto t = eventuals::task(s);

  eventuals::start(t);

  eventuals::stop(t);

  EXPECT_THROW(eventuals::wait(t), StoppedException);
}


TEST(StreamTest, Transform)
{
    // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop, done;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  EXPECT_CALL(done, Call())
    .Times(0);

  auto s = stream<int>(
      5,
      /* next = */ [](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      },
      /* done = */ [&](auto&, auto&) {
        done.Call();
      },
      /* stop = */ [&](auto&, auto&) {
        stop.Call();
      })
    | [](int i) { return i + 1; }
    | loop<int>(
        0,
        /* start = */ [](auto&, auto& stream) {
          next(stream);
        },
        /* body = */ [](auto& sum, auto& stream, auto&& value) {
          sum += value;
          next(stream);
        },
        /* ended = */ [](auto& sum, auto& k) {
          succeed(k, sum);
        },
        /* fail = */ [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        /* stop = */ [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_EQ(20, eventuals::run(eventuals::task(s)));
}


TEST(StreamTest, MapReduce)
{
    // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> stop, done;

  EXPECT_CALL(stop, Call())
    .Times(0);

  EXPECT_CALL(done, Call())
    .Times(0);

  auto s = stream<int>(
      5,
      /* next = */ [](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      },
      /* done = */ [&](auto&, auto&) {
        done.Call();
      },
      /* stop = */ [&](auto&, auto&) {
        stop.Call();
      })
    | map<int>([](int i) { return i + 1; })
    | reduce<int>(0, [](auto&& sum, auto&& value) { return sum + value; });

  EXPECT_EQ(20, eventuals::run(eventuals::task(s)));
}
