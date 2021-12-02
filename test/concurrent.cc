#include "eventuals/concurrent.h"

#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/concurrent-ordered.h"
#include "eventuals/eventual.h"
#include "eventuals/flat-map.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/range.h"
#include "eventuals/reduce.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Callback;
using eventuals::Collect;
using eventuals::Concurrent;
using eventuals::ConcurrentOrdered;
using eventuals::Eventual;
using eventuals::FlatMap;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Map;
using eventuals::Range;
using eventuals::Reduce;
using eventuals::Stream;
using eventuals::Terminate;
using eventuals::Then;

using testing::Contains;

// NOTE: For 'ConcurrentOrdered' checks.
using testing::ElementsAre;

// NOTE: using 'UnorderedElementsAre' since semantics of
// 'Concurrent()' may result in unordered execution, even though the
// tests might have be constructed deterministically.
using testing::UnorderedElementsAre;

struct ConcurrentType {};
struct ConcurrentOrderedType {};

template <typename Type>
class ConcurrentTypedTest : public testing::Test {
 public:
  template <typename F>
  auto ConcurrentOrConcurrentOrdered(F f) {
    if constexpr (std::is_same_v<Type, ConcurrentType>) {
      return Concurrent(std::move(f));
    } else {
      return ConcurrentOrdered(std::move(f));
    }
  }

  template <typename... Args>
  auto OrderedOrUnorderedElementsAre(Args&&... args) {
    if constexpr (std::is_same_v<Type, ConcurrentType>) {
      return UnorderedElementsAre(std::forward<Args>(args)...);
    } else {
      return ElementsAre(std::forward<Args>(args)...);
    }
  }
};

using ConcurrentTypes = ::testing::Types<
    ConcurrentType,
    ConcurrentOrderedType>;
TYPED_TEST_SUITE(ConcurrentTypedTest, ConcurrentTypes);

// Tests when all eventuals are successful.
TYPED_TEST(ConcurrentTypedTest, Success) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    data.i = i;
                    callbacks.emplace_back([&data]() {
                      static_cast<K*>(data.k)->Start(std::to_string(data.i));
                    });
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

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre("1", "2"));
}

//Tests when at least one of the eventuals stops.
TYPED_TEST(ConcurrentTypedTest, Stop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
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
TYPED_TEST(ConcurrentTypedTest, Fail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
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
TYPED_TEST(ConcurrentTypedTest, FailOrStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
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
  // Expecting 'StoppedException' for 'ConcurrentOrdered'.
  if constexpr (std::is_same_v<TypeParam, ConcurrentType>) {
    EXPECT_ANY_THROW(future.get());
  } else {
    EXPECT_THROW(future.get(), eventuals::StoppedException);
  }
}

// Tests when an eventuals stops before an eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, StopBeforeStart) {
  Callback<> start;
  Callback<> stop;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    data.i = i;
                    if (data.i == 1) {
                      start = [&data]() {
                        static_cast<K*>(data.k)->Start(std::to_string(data.i));
                      };
                    } else {
                      stop = [&data]() {
                        static_cast<K*>(data.k)->Stop();
                      };
                    }
                  });
            }));
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_TRUE(start);
  EXPECT_TRUE(stop);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  // NOTE: executing 'stop' before 'start'.
  stop();
  start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests when an eventuals fails before an eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, FailBeforeStart) {
  Callback<> start;
  Callback<> fail;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    data.i = i;
                    if (data.i == 1) {
                      start = [&data]() {
                        static_cast<K*>(data.k)->Start(std::to_string(data.i));
                      };
                    } else {
                      fail = [&data]() {
                        static_cast<K*>(data.k)->Fail("error");
                      };
                    }
                  });
            }));
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_TRUE(start);
  EXPECT_TRUE(fail);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  // NOTE: executing 'fail' before 'start'.
  fail();
  start();

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case each eventual
// ignores interrupts so we'll successfully collect all the values.
TYPED_TEST(ConcurrentTypedTest, InterruptSuccess) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    data.i = i;
                    callbacks.emplace_back([&data]() {
                      static_cast<K*>(data.k)->Start(std::to_string(data.i));
                    });
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

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre("1", "2"));
}

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case one all of
// the eventuals will stop so the result will be a stop.
TYPED_TEST(ConcurrentTypedTest, InterruptStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
                    handler.Install([&k]() {
                      k.Stop();
                    });
                    callbacks.emplace_back([]() {});
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

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case one of the
// eventuals will stop and one will fail so the result will be either
// a fail or stop. 'Fail' for 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, InterruptFailOrStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
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

  // NOTE: expecting "any" throwable here depending on whether the
  // eventual that stopped or failed was completed first.
  // Expecting 'std::exception_ptr' for 'ConcurrentOrdered'.
  if constexpr (std::is_same_v<TypeParam, ConcurrentType>) {
    EXPECT_ANY_THROW(future.get());
  } else {
    EXPECT_THROW(future.get(), std::exception_ptr);
  }
}

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case both of the
// eventuals will fail so the result will be a fail.
TYPED_TEST(ConcurrentTypedTest, InterruptFail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
                    handler.Install([&k]() {
                      k.Fail("error");
                    });
                    callbacks.emplace_back([]() {});
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

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests when when upstream stops the result will be stop.
TYPED_TEST(ConcurrentTypedTest, StreamStop) {
  auto e = [&]() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Stop();
               })
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests when when upstream fails the result will be fail.
TYPED_TEST(ConcurrentTypedTest, StreamFail) {
  auto e = [&]() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Fail("error");
               })
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Tests when when upstream stops after an interrupt the result will
// be stop.
TYPED_TEST(ConcurrentTypedTest, EmitInterruptStop) {
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
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
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
TYPED_TEST(ConcurrentTypedTest, EmitInterruptFail) {
  auto e = [&]() {
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
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
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
TYPED_TEST(ConcurrentTypedTest, EmitFailInterrupt) {
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
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>([&](auto& k) {
                k.Fail("error");
                interrupt.Trigger();
              });
            }));
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_THROW(future.get(), std::exception_ptr);
}

// Same as 'EmitFailInterrupt' except each eventual stops not fails.
TYPED_TEST(ConcurrentTypedTest, EmitStopInterrupt) {
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
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>([&](auto& k) {
                k.Stop();
                interrupt.Trigger();
              });
            }));
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

// Tests what happens when downstream is done before 'Concurrent()' is
// done and each eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, DownstreamDoneBothEventualsSuccess) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .start([&](auto& k) mutable {
                    if (i == 1) {
                      callbacks.emplace_back([&k]() {
                        k.Start("1");
                      });
                    } else {
                      callbacks.emplace_back([&k]() {
                        k.Start("2");
                      });
                    }
                  });
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

  if constexpr (std::is_same_v<TypeParam, ConcurrentType>) {
    EXPECT_THAT(values, Contains(future.get()));
  } else {
    EXPECT_EQ(values[0], future.get());
  }
}

// Tests what happens when downstream is done before 'Concurrent()' is
// done and one eventual stops.
TYPED_TEST(ConcurrentTypedTest, DownstreamDoneOneEventualStop) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
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
                  });
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
TYPED_TEST(ConcurrentTypedTest, DownstreamDoneOneEventualFail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
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
                  });
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

// Tests that one can nest 'FlatMap()' within a
// 'Concurrent()' or 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, FlatMap) {
  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([]() {
            return FlatMap([](int i) {
              return Range(i);
            });
          })
        | Collect<std::vector<int>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre(0, 0, 1));
}

// Tests that only moveable values will be moved into
// 'Concurrent()' and 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, Moveable) {
  struct Moveable {
    Moveable() = default;
    Moveable(Moveable&&) = default;
  };

  auto e = [&]() {
    return Iterate({Moveable()})
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map(Let([](auto& moveable) {
              return 42;
            }));
          })
        | Collect<std::vector<int>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre(42));
}