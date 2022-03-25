#include "eventuals/iterate.h"

#include <array>
#include <deque>
#include <initializer_list>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/reduce.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"

using eventuals::Iterate;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Reduce;
using eventuals::Then;

TEST(Iterate, VectorLvalue) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, VectorBeginEnd) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v.begin(), v.end() - 1)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(5, *s());
}


TEST(Iterate, VectorRvalue) {
  auto s = [&]() {
    return Iterate(std::vector<int>({5, 12}))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, VectorMove) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(v))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, v.size());
}


TEST(Iterate, SetLvalue) {
  std::set<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, SetBeginEnd) {
  std::set<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container.begin(), container.end())
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, SetRvalue) {
  auto s = [&]() {
    return Iterate(std::set<int>({5, 12}))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, SetMove) {
  std::set<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, container.size());
}


TEST(Iterate, ListLvalue) {
  std::list<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, ListBeginEnd) {
  std::list<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container.begin(), container.end())
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, ListRvalue) {
  auto s = [&]() {
    return Iterate(std::list<int>({5, 12}))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, ListMove) {
  std::list<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, container.size());
}


TEST(Iterate, DequeLvalue) {
  std::deque<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, DequeBeginEnd) {
  std::deque<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container.begin() + 1, container.end())
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(12, *s());
}


TEST(Iterate, DequeRvalue) {
  auto s = [&]() {
    return Iterate(std::deque<int>({5, 12}))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, DequeMove) {
  std::deque<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, container.size());
}


TEST(Iterate, MapLvalue) {
  std::map<int, int> container = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, MapBeginEnd) {
  std::map<int, int> container = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(container.begin(), container.end())
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, MapRvalue) {
  auto s = [&]() {
    return Iterate(std::map<int, int>{{1, 5}, {2, 12}})
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, MapMove) {
  std::map<int, int> container = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, container.size());
}


TEST(Iterate, UnorderedSetLvalue) {
  std::unordered_set<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, UnorderedSetBeginEnd) {
  std::unordered_set<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(container.begin(), container.end())
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, UnorderedSetRvalue) {
  auto s = [&]() {
    return Iterate(std::unordered_set<int>{5, 12})
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, UnorderedSetMove) {
  std::unordered_set<int> container = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, container.size());
}


TEST(Iterate, UnorderedMapLvalue) {
  std::unordered_map<int, int> container = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, UnorderedMapBeginEnd) {
  std::unordered_map<int, int> container = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(container.begin(), container.end())
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, UnorderedMapRvalue) {
  auto s = [&]() {
    return Iterate(std::unordered_map<int, int>{{1, 5}, {2, 12}})
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, UnorderedMapMove) {
  std::unordered_map<int, int> container = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value.second;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
  EXPECT_EQ(0, container.size());
}


TEST(Iterate, ArrayLvalue) {
  std::array<int, 2> container = {5, 12};

  auto s = [&]() {
    return Iterate(container)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, ArrayBeginEnd) {
  std::array<int, 2> container = {5, 12};

  auto s = [&]() {
    return Iterate(container.begin(), container.end() - 1)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(5, *s());
}


TEST(Iterate, ArrayRvalue) {
  auto s = [&]() {
    return Iterate(std::array<int, 2>({5, 12}))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, ArrayMove) {
  std::array<int, 2> container = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, ArrayStringMove) {
  std::array<std::string, 2> container = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(std::move(container))
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ("HelloWorld", *s());
  EXPECT_EQ("", container[0]);
  EXPECT_EQ("", container[1]);
}


TEST(Iterate, CommonArrayPointer) {
  int container[] = {5, 12};

  auto s = [&]() {
    return Iterate(&container[0], &container[1] + 1)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, CommonArraySize) {
  int container[] = {5, 12};

  auto s = [&]() {
    return Iterate(container, 2)
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(17, *s());
}


TEST(Iterate, VectorStringConcatenate) {
  std::vector<std::string> container = {"Hello", "World", "!"};

  auto s = [&]() {
    return Iterate(container)
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& s, auto& stream, auto&& value) {
                if (!s.empty()) {
                  s += ' ';
                }
                s += value;
                stream.Next();
              })
              .ended([](auto& s, auto& k) {
                k.Start(s);
              });
  };

  EXPECT_EQ("Hello World !", *s());
}


TEST(Iterate, VectorStringContcatenatePartial) {
  std::vector<std::string> container = {"...", "..", "Hello", "World", "!"};

  auto s = [&]() {
    return Iterate(container.begin() + 2, container.end() - 1)
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& s, auto& stream, auto&& value) {
                if (!s.empty()) {
                  s += ' ';
                }
                s += value;
                stream.Next();
              })
              .ended([](auto& s, auto& k) {
                k.Start(s);
              });
  };

  EXPECT_EQ("Hello World", *s());
}

TEST(Iterate, InitializerList) {
  auto s = []() {
    return Iterate({5, 12, 13})
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(30, *s());
}

TEST(Iterate, UniquePtr) {
  std::vector<std::unique_ptr<int>> v;

  v.emplace_back(std::make_unique<int>(1));
  v.emplace_back(std::make_unique<int>(2));

  auto s = [&]() {
    return Iterate(v)
        | Map([](auto& i) -> decltype(i) {
             (*i)++;
             return i;
           })
        | Reduce(
               /* sum = */ 0,
               [](auto& sum) {
                 return Then([&](auto& i) {
                   sum += *i;
                   return true;
                 });
               });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(s())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(5, *s());
}
