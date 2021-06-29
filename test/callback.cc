#include "gtest/gtest.h"

#include "stout/callback.h"

using stout::Callback;

TEST(Callback, Destructor)
{
  struct Foo
  {
    Foo(bool* destructed) : destructed_(destructed) {}

    Foo(Foo&& that) : destructed_(that.destructed_)
    {
      that.destructed_ = nullptr;
    }

    ~Foo()
    {
      if (destructed_ != nullptr) {
        *destructed_ = true;
      }
    }
    bool* destructed_ = nullptr;
  };

  bool destructed = false;

  {
    Callback<> callback = [foo = Foo { &destructed }]() {};
    callback();
    EXPECT_FALSE(destructed);
  }

  EXPECT_TRUE(destructed);
}
