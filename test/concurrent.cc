#include "eventuals/concurrent.h"

#include <deque>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/let.h"
#include "eventuals/range.h"
#include "eventuals/reduce.h"
#include "eventuals/stream-for-each.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Callback;
using eventuals::Collect;
using eventuals::Concurrent;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Range;
using eventuals::Reduce;
using eventuals::Stream;
using eventuals::StreamForEach;
using eventuals::Terminate;
using eventuals::Then;

using testing::Contains;

// NOTE: using 'UnorderedElementsAre' since semantics of
// 'Concurrent()' may result in unordered execution, even though the
// tests might have be constructed deterministically.
using testing::UnorderedElementsAre;

// Tests when all eventuals are successful.
TEST(ConcurrentTest, Success) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             struct Data {
               void* k;
               int i;
             };
             return Map(Eventual<std::string>(
                 [&, data = Data()](auto& k, int i) mutable {
                   using K = std::decay_t<decltype(k)>;
                   data.k = &k;
                   data.i = i;
                   callbacks.emplace_back([&data]() {
                     static_cast<K*>(data.k)->Start(std::to_string(data.i));
                   });
                 }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_THAT(future.get(), UnorderedElementsAre("1", "2"));
}

// Tests when at least one of the eventuals stops.
TEST(ConcurrentTest, Stop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             struct Data {
               void* k;
               int i;
             };
             return Map(Eventual<std::string>(
                 [&, data = Data()](auto& k, int i) mutable {
                   using K = std::decay_t<decltype(k)>;
                   data.k = &k;
                   data.i = i;
                   callbacks.emplace_back([&data]() {
                     if (data.i == 1) {
                       static_cast<K*>(data.k)->Start(std::to_string(data.i));
                     } else {
                       static_cast<K*>(data.k)->Stop();
                     }
                   });
                 }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests when at least one of the eventuals fails.
TEST(ConcurrentTest, Fail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             struct Data {
               void* k;
               int i;
             };
             return Map(Eventual<std::string>(
                 [&, data = Data()](auto& k, int i) mutable {
                   using K = std::decay_t<decltype(k)>;
                   data.k = &k;
                   data.i = i;
                   callbacks.emplace_back([&data]() {
                     if (data.i == 1) {
                       static_cast<K*>(data.k)->Start(std::to_string(data.i));
                     } else {
                       static_cast<K*>(data.k)->Fail("error");
                     }
                   });
                 }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests when every eventual either stops or fails.
TEST(ConcurrentTest, FailOrStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             struct Data {
               void* k;
               int i;
             };
             return Map(Eventual<std::string>(
                 [&, data = Data()](auto& k, int i) mutable {
                   using K = std::decay_t<decltype(k)>;
                   data.k = &k;
                   data.i = i;
                   callbacks.emplace_back([&data]() {
                     if (data.i == 1) {
                       static_cast<K*>(data.k)->Stop();
                     } else {
                       static_cast<K*>(data.k)->Fail("error");
                     }
                   });
                 }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  // NOTE: expecting "any" throwable here depending on whether the
  // eventual that stopped or failed was completed first.
  EXPECT_ANY_THROW(future.get());
}

// Tests that 'Concurrent()' defers to the eventuals on how to handle
// interrupts and in this case each eventual ignores interrupts so
// we'll successfully collect all the values.
TEST(ConcurrentTest, InterruptSuccess) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             struct Data {
               void* k;
               int i;
             };
             return Map(Eventual<std::string>(
                 [&, data = Data()](auto& k, int i) mutable {
                   using K = std::decay_t<decltype(k)>;
                   data.k = &k;
                   data.i = i;
                   callbacks.emplace_back([&data]() {
                     static_cast<K*>(data.k)->Start(std::to_string(data.i));
                   });
                 }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_THAT(future.get(), UnorderedElementsAre("1", "2"));
}

// Tests that 'Concurrent()' defers to the eventuals on how to handle
// interrupts and in this case one all of the eventuals will stop so
// the result will be a stop.
TEST(ConcurrentTest, InterruptStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             return Map(
                 Eventual<std::string>()
                     .interruptible()
                     .start([&](auto& k,
                                Interrupt::Handler& handler,
                                int i) mutable {
                       handler.Install([&k]() {
                         k.Stop();
                       });
                       callbacks.emplace_back([]() {});
                     }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests that 'Concurrent()' defers to the eventuals on how to handle
// interrupts and in this case one of the eventuals will stop and one
// will fail so the result will be either a fail or stop.
TEST(ConcurrentTest, InterruptFailOrStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             return Map(
                 Eventual<std::string>()
                     .interruptible()
                     .start([&](auto& k,
                                Interrupt::Handler& handler,
                                int i) mutable {
                       if (i == 1) {
                         handler.Install([&k]() {
                           k.Stop();
                         });
                       } else {
                         handler.Install([&k]() {
                           k.Fail("error");
                         });
                       }

                       callbacks.emplace_back([]() {});
                     }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  // NOTE: expecting "any" throwable here depending on whether the
  // eventual that stopped or failed was completed first.
  EXPECT_ANY_THROW(future.get());
}

// Tests that 'Concurrent()' defers to the eventuals on how to handle
// interrupts and in this case both of the eventuals will fail so the
// result will be a fail.
TEST(ConcurrentTest, InterruptFail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             return Map(
                 Eventual<std::string>()
                     .interruptible()
                     .start([&](auto& k,
                                Interrupt::Handler& handler,
                                int i) mutable {
                       handler.Install([&k]() {
                         k.Fail("error");
                       });
                       callbacks.emplace_back([]() {});
                     }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests when when upstream stops the result will be stop.
TEST(ConcurrentTest, StreamStop) {
  auto e = []() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Stop();
               })
        | Concurrent([]() {
             return Map(Then([](int i) {
               return std::to_string(i);
             }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests when when upstream fails the result will be fail.
TEST(ConcurrentTest, StreamFail) {
  auto e = []() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Fail("error");
               })
        | Concurrent([]() {
             return Map(Then([](int i) {
               return std::to_string(i);
             }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests when when upstream stops after an interrupt the result will
// be stop.
TEST(ConcurrentTest, EmitInterruptStop) {
  auto e = []() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Stop();
                 });
                 k.Begin();
               })
               .next([i = 0](auto& k) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
               })
        | Concurrent([]() {
             return Map(Then([](int i) {
               return std::to_string(i);
             }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests when when upstream fails after an interrupt the result will
// be fail.
TEST(ConcurrentTest, EmitInterruptFail) {
  auto e = []() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Fail("error");
                 });
                 k.Begin();
               })
               .next([i = 0](auto& k) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
               })
        | Concurrent([]() {
             return Map(Then([](int i) {
               return std::to_string(i);
             }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests that when one of the 'Concurrent()' eventuals fails it can
// ensure that everything correctly fails by "interrupting"
// upstream. In this case we interrupt upstream by using an
// 'Interrupt' but there may diffrent ways of doing it depending on
// what you're building. See the TODO in
// '_Concurrent::TypeErasedAdaptor::Done()' for more details on the
// semantics of 'Concurrent()' that are important to consider here.
TEST(ConcurrentTest, EmitFailInterrupt) {
  Interrupt interrupt;

  auto e = [&]() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Stop();
                 });
                 k.Begin();
               })
               .next([i = 0](auto& k) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
               })
        | Concurrent([&]() {
             return Map(Eventual<std::string>([&](auto& k, int i) {
               k.Fail("error");
               interrupt.Trigger();
             }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_ANY_THROW(future.get());
}

// Same as 'EmitFailInterrupt' except each eventual stops not fails.
TEST(ConcurrentTest, EmitStopInterrupt) {
  Interrupt interrupt;

  auto e = [&]() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Stop();
                 });
                 k.Begin();
               })
               .next([i = 0](auto& k) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
               })
        | Concurrent([&]() {
             return Map(Eventual<std::string>([&](auto& k, int i) {
               k.Stop();
               interrupt.Trigger();
             }));
           })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_ANY_THROW(future.get());
}

// Tests what happens when downstream is done before 'Concurrent()' is
// done and each eventual succeeds.
TEST(ConcurrentTest, DownstreamDoneBothEventualsSuccess) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             return Map(Eventual<std::string>()
                            .start([&](auto& k, int i) mutable {
                              if (i == 1) {
                                callbacks.emplace_back([&k]() {
                                  k.Start("1");
                                });
                              } else {
                                callbacks.emplace_back([&k]() {
                                  k.Start("2");
                                });
                              }
                            }));
           })
        | Reduce(
               std::string(),
               [](auto& result) {
                 return Then([&](auto&& value) {
                   result = value;
                   return false; // Only take the first element!
                 });
               });
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  std::vector<std::string> values = {"1", "2"};

  EXPECT_THAT(values, Contains(future.get()));
}

// Tests what happens when downstream is done before 'Concurrent()' is
// done and one eventual stops.
TEST(ConcurrentTest, DownstreamDoneOneEventualStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             return Map(
                 Eventual<std::string>()
                     .interruptible()
                     .start([&](auto& k,
                                Interrupt::Handler& handler,
                                int i) mutable {
                       if (i == 1) {
                         callbacks.emplace_back([&k]() {
                           k.Start("1");
                         });
                       } else {
                         handler.Install([&k]() {
                           k.Stop();
                         });

                         callbacks.emplace_back([]() {});
                       }
                     }));
           })
        | Reduce(
               std::string(),
               [](auto& result) {
                 return Then([&](auto&& value) {
                   result = value;
                   return false; // Only take the first element!
                 });
               });
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_EQ("1", future.get());
}

// Tests what happens when downstream is done before 'Concurrent()' is
// done and one eventual fails.
TEST(ConcurrentTest, DownstreamDoneOneEventualFail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | Concurrent([&]() {
             return Map(
                 Eventual<std::string>()
                     .interruptible()
                     .start([&](auto& k,
                                Interrupt::Handler& handler,
                                int i) mutable {
                       if (i == 1) {
                         callbacks.emplace_back([&k]() {
                           k.Start("1");
                         });
                       } else {
                         handler.Install([&k]() {
                           k.Fail("error");
                         });
                         callbacks.emplace_back([]() {});
                       }
                     }));
           })
        | Reduce(
               std::string(),
               [](auto& result) {
                 return Then([&](auto&& value) {
                   result = value;
                   return false; // Only take the first element!
                 });
               });
  };

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_EQ("1", future.get());
}

// Tests that one can nest 'StreamForEach()' within a 'Concurrent()'.
TEST(ConcurrentTest, StreamForEach) {
  auto e = []() {
    return Iterate({1, 2})
        | Concurrent([]() {
             return StreamForEach([](int i) {
               return Range(i);
             });
           })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e(), UnorderedElementsAre(0, 0, 1));
}

// Tests that only moveable values will be moved into 'Concurrent()'.
TEST(ConcurrentTest, Moveable) {
  struct Moveable {
    Moveable() = default;
    Moveable(Moveable&&) = default;
  };

  auto e = []() {
    return Iterate({Moveable()})
        | Concurrent([]() {
             return Map(Then(Let([](auto& moveable) {
               return 42;
             })));
           })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e(), UnorderedElementsAre(42));
}
