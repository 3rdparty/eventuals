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

// std transform + arrays ????


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
  std::set<int> set_ = {5, 12};

  auto s = [&]() {
    return Iterate(set_)
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
  std::set<int> set_ = {5, 12};

  auto s = [&]() {
    return Iterate(set_.begin(), set_.end())
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
  std::set<int> set_ = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(set_))
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
  EXPECT_EQ(0, set_.size());
}

TEST(Iterate, ListElemSum) {
  std::list<int> list_ = {5, 12};

  auto s = [&]() {
    return Iterate(list_)
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
  std::list<int> list_ = {5, 12};

  auto s = [&]() {
    return Iterate(list_.begin(), list_.end())
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
  std::list<int> list_ = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(list_))
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
  EXPECT_EQ(0, list_.size());
}

TEST(Iterate, DequeElemSum) {
  std::deque<int> deque_ = {5, 12};

  auto s = [&]() {
    return Iterate(deque_)
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
  std::deque<int> deque_ = {5, 12};

  auto s = [&]() {
    return Iterate(deque_.begin() + 1, deque_.end())
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
  std::deque<int> deque_ = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(deque_))
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
  EXPECT_EQ(0, deque_.size());
}

TEST(Iterate, MapElemSum) {
  std::map<int, int> map_ = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(map_)
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
  std::map<int, int> map_ = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(map_.begin(), map_.end())
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
  std::map<int, int> map_ = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(std::move(map_))
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
  EXPECT_EQ(0, map_.size());
}

TEST(Iterate, UnorderedSetElemSum) {
  std::unordered_set<int> uset_ = {5, 12};

  auto s = [&]() {
    return Iterate(uset_)
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
  std::unordered_set<int> uset_ = {5, 12};

  auto s = [&]() {
    return Iterate(uset_.begin(), uset_.end())
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
  std::unordered_set<int> uset_ = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(uset_))
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
  EXPECT_EQ(0, uset_.size());
}

TEST(Iterate, UnorderedMapElemSum) {
  std::unordered_map<int, int> umap_ = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(umap_)
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
  std::unordered_map<int, int> umap_ = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(umap_.begin(), umap_.end())
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
  std::unordered_map<int, int> umap_ = {{1, 5}, {2, 12}};

  auto s = [&]() {
    return Iterate(std::move(umap_))
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
  EXPECT_EQ(0, umap_.size());
}

TEST(Iterate, ArrayElemSum) {
  std::array<int, 2> array_ = {5, 12};

  auto s = [&]() {
    return Iterate(array_)
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
  std::array<int, 2> array_ = {5, 12};

  auto s = [&]() {
    return Iterate(array_.begin(), array_.end() - 1)
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
  std::array<int, 2> array_ = {5, 12};

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
  EXPECT_EQ(2, array_.size());
}

TEST(Iterate, ArrayElemSumMove) {
  std::array<int, 2> array_ = {5, 12};

  auto s = [&]() {
    return Iterate(std::move(array_))
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
  EXPECT_EQ(2, array_.size());
}

TEST(Iterate, ArrayMovableElemSumMove) {
  std::array<std::string, 2> array_ = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(std::move(array_))
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
  EXPECT_EQ(2, array_.size());
  EXPECT_EQ("", array_[0]);
  EXPECT_EQ("", array_[1]);
}

TEST(Iterate, CommonArrayElemSumPtr) {
  int x[] = {5, 12};

  auto s = [&]() {
    return Iterate(&x[0], &x[1] + 1)
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
  int x[] = {5, 12};

  auto s = [&]() {
    return Iterate(x, 2)
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
  std::vector<std::string> v = {"Hello", "World", "!"};

  auto s = [&]() {
    return Iterate(v)
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& str, auto& stream, auto&& value) {
                if (!str.empty())
                  str += ' ';
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
  std::vector<std::string> v = {"...", "..", "Hello", "World", "!"};

  auto s = [&]() {
    return Iterate(v.begin() + 2, v.end() - 1)
        | Loop<std::string>()
              .context(std::string(""))
              .body([](auto& str, auto& stream, auto&& value) {
                if (!str.empty())
                  str += ' ';
                str += value;
                stream.Next();
              })
              .ended([](auto& str, auto& k) {
                k.Start(str);
              });
  };

  EXPECT_EQ("Hello World", *s());
}