#include "stout/iterate.h"

#include <array>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Context;
using stout::eventuals::Iterate;
using stout::eventuals::Loop;

TEST(Iterate, VectorElemSum) {
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

TEST(Iterate, VectorElemSumIter) {
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

TEST(Iterate, VectorElemSumRvalue) {
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

TEST(Iterate, VectorElemSumMove) {
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

TEST(Iterate, SetElemSum) {
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

TEST(Iterate, SetElemSumIter) {
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

TEST(Iterate, SetElemSumRvalue) {
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

TEST(Iterate, SetElemSumMove) {
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

TEST(Iterate, ListElemSum) {
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

TEST(Iterate, ListElemSumIter) {
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

TEST(Iterate, ListElemSumRvalue) {
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

TEST(Iterate, ListElemSumMove) {
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

TEST(Iterate, DequeElemSum) {
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

TEST(Iterate, DequeElemSumIter) {
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

TEST(Iterate, DequeElemSumRvalue) {
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

TEST(Iterate, DequeElemSumMove) {
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

TEST(Iterate, MapElemSum) {
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

TEST(Iterate, MapElemSumIter) {
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

TEST(Iterate, MapElemSumRvalue) {
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

TEST(Iterate, MapElemSumMove) {
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

TEST(Iterate, UnorderedSetElemSum) {
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

TEST(Iterate, UnorderedSetElemSumIter) {
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

TEST(Iterate, UnorderedSetElemSumRvalue) {
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

TEST(Iterate, UnorderedSetElemSumMove) {
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

TEST(Iterate, UnorderedMapElemSum) {
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

TEST(Iterate, UnorderedMapElemSumIter) {
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

TEST(Iterate, UnorderedMapElemSumRvalue) {
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

TEST(Iterate, UnorderedMapElemSumMove) {
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

TEST(Iterate, ArrayElemSum) {
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

TEST(Iterate, ArrayElemSumIter) {
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

TEST(Iterate, ArrayElemSumRvalue) {
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

TEST(Iterate, ArrayElemSumMove) {
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

TEST(Iterate, ArrayMovableElemSumMove) {
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

TEST(Iterate, CommonArrayElemSumPtr) {
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

TEST(Iterate, CommonArrayElemSum) {
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

TEST(Iterate, VectorStringConcat) {
  std::vector<std::string> container = {"Hello", "World", "!"};

  auto s = [&]() {
    return Iterate(container)
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& str, auto& stream, auto&& value) {
                if (!str.empty()) {
                  str += ' ';
                }
                str += value;
                stream.Next();
              })
              .ended([](auto& str, auto& k) {
                k.Start(str);
              });
  };

  EXPECT_EQ("Hello World !", *s());
}

TEST(Iterate, VectorStringContcatPart) {
  std::vector<std::string> container = {"...", "..", "Hello", "World", "!"};

  auto s = [&]() {
    return Iterate(container.begin() + 2, container.end() - 1)
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& str, auto& stream, auto&& value) {
                if (!str.empty()) {
                  str += ' ';
                }
                str += value;
                stream.Next();
              })
              .ended([](auto& str, auto& k) {
                k.Start(str);
              });
  };

  EXPECT_EQ("Hello World", *s());
}