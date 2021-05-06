#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/stream.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::ended;
using stout::eventuals::Loop;
using stout::eventuals::map;
using stout::eventuals::reduce;
using stout::eventuals::Stream;
using stout::eventuals::succeed;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

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

  auto s = Stream<int>()
    .context(5)
    .next([](auto& count, auto& k) {
      if (count > 0) {
        emit(k, count--);
      } else {
        ended(k);
      }
    })
    .done([&](auto&, auto&) {
      done.Call();
    })
    .stop([&](auto&, auto&) {
      stop.Call();
    })
    | (Loop<int>()
       .context(0)
       .body([](auto& sum, auto& stream, auto&& value) {
         sum += value;
         next(stream);
       })
       .ended([](auto& sum, auto& k) {
         succeed(k, sum);
       })
       .fail([&](auto&, auto&, auto&&) {
         fail.Call();
       })
       .stop([&](auto&, auto&) {
         stop.Call();
       }));

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

  auto s = Stream<int>()
    .context(0)
    .next([](auto& value, auto& k) {
      emit(k, value);
    })
    .done([](auto&, auto& k) {
      ended(k);
    })
    .stop([&](auto&, auto&) {
      stop.Call();
    })
    | (Loop<int>()
       .context(0)
       .body([](auto& count, auto& stream, auto&&) {
         if (++count == 2) {
           done(stream);
         } else {
           next(stream);
         }
       })
       .ended([](auto& count, auto& k) {
         succeed(k, count);
       })
       .fail([&](auto&, auto&, auto&&) {
         fail.Call();
       })
       .stop([&](auto&, auto&) {
         stop.Call();
       }));

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

  auto s = Stream<int>()
    .context("error")
    .next([](auto& error, auto& k) {
      eventuals::fail(k, error);
    })
    .done([&](auto&, auto&) {
      done.Call();
    })
    .stop([&](auto&, auto&) {
      stop.Call();
    })
    | (Loop<int>()
       .context(0)
       .body([](auto&, auto& stream, auto&&) {
         next(stream);
       })
       .ended([&](auto&, auto&) {
         ended.Call();
       })
       .fail([&](auto&, auto& k, auto&& error) {
         eventuals::fail(k, std::forward<decltype(error)>(error));
       })
       .stop([&](auto&, auto&) {
         stop.Call();
       }));

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

  auto s = Stream<int>()
    .next([](auto& k) {
      emit(k, 0);
    })
    .done([&](auto&) {
      done.Call();
    })
    .stop([](auto& k) {
      stop(k);
    })
    | (Loop<int>()
       .context(0)
       .body([&](auto&, auto&, auto&&) {
         body.Call();
       })
       .ended([&](auto&, auto&) {
         ended.Call();
       })
       .fail([&](auto&, auto&, auto&&) {
         fail.Call();
       })
       .stop([](auto&, auto& k) {
         stop(k);
       }));

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

  auto s = Stream<int>()
    .context(5)
    .next([](auto& count, auto& k) {
      if (count > 0) {
        emit(k, count--);
      } else {
        ended(k);
      }
    })
    .done([&](auto&, auto&) {
      done.Call();
    })
    .stop([&](auto&, auto&) {
      stop.Call();
    })
    | [](int i) { return i + 1; }
    | (Loop<int>()
       .context(0)
       .body([](auto& sum, auto& stream, auto&& value) {
         sum += value;
         next(stream);
       })
       .ended([](auto& sum, auto& k) {
         succeed(k, sum);
       }));

  EXPECT_EQ(20, eventuals::run(eventuals::task(s)));
}


TEST(StreamTest, MapReduce)
{
  auto s = Stream<int>()
    .context(5)
    .next([](auto& count, auto& k) {
      if (count > 0) {
        emit(k, count--);
      } else {
        ended(k);
      }
    })
    | map<int>([](int i) {
      return i + 1;
    })
    | reduce<int>(
        /* sum = */ 0,
        [](auto&& sum, auto&& value) {
          return sum + value;
        });

  EXPECT_EQ(20, eventuals::run(eventuals::task(s)));
}
