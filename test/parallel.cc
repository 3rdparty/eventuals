#include "eventuals/parallel.h"

#include <set>

#include "eventuals/eventual.h"
#include "eventuals/raise.h"
#include "eventuals/reduce.h"
#include "eventuals/terminal.h"
#include "gtest/gtest.h"

using eventuals::Eventual;
using eventuals::Parallel;
using eventuals::Raise;
using eventuals::Reduce;
using eventuals::Stream;
using eventuals::Then;

TEST(ParallelTest, Succeed) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Parallel([]() {
             return Then([](int i) {
               std::this_thread::sleep_for(std::chrono::milliseconds(100));
               return i + 1;
             });
           })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Then([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  auto values = *s();

  EXPECT_TRUE(values.empty());
}


TEST(ParallelTest, Done) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Parallel([]() {
             return Then([](int i) {
               std::this_thread::sleep_for(std::chrono::milliseconds(100));
               return i + 1;
             });
           })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Then([&values](auto&& value) {
                   values.erase(value);
                   return false;
                 });
               });
  };

  auto values = *s();

  EXPECT_EQ(4, values.size());
}


TEST(ParallelTest, IngressFail) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Fail("error");
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Parallel([]() {
             return Then([](int i) {
               std::this_thread::sleep_for(std::chrono::milliseconds(100));
               return i + 1;
             });
           })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Then([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), std::exception_ptr);
}


TEST(ParallelTest, IngressStop) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Stop();
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Parallel([]() {
             return Then([](int i) {
               std::this_thread::sleep_for(std::chrono::milliseconds(100));
               return i + 1;
             });
           })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Then([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), eventuals::StoppedException);
}


TEST(ParallelTest, WorkerFail) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Emit(count--);
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Parallel([]() {
             return Raise("error");
           })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Then([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), std::exception_ptr);
}


TEST(ParallelTest, WorkerStop) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Emit(count--);
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Parallel([]() {
             return Eventual<int>()
                 .start([](auto& k, auto&&...) {
                   k.Stop();
                 });
           })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Then([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), eventuals::StoppedException);
}
