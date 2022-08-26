#include "eventuals/callback.hh"

#include "gtest/gtest.h"
#include "stout/borrowed_ptr.h"

namespace eventuals::test {
namespace {

TEST(Callback, Destructor) {
  struct Foo {
    Foo(bool* destructed)
      : destructed_(destructed) {}

    Foo(Foo&& that)
      : destructed_(that.destructed_) {
      that.destructed_ = nullptr;
    }

    ~Foo() {
      if (destructed_ != nullptr) {
        *destructed_ = true;
      }
    }
    bool* destructed_ = nullptr;
  };

  bool destructed = false;

  {
    Callback<void()> callback = [foo = Foo{&destructed}]() {};
    callback();
    EXPECT_FALSE(destructed);
  }

  EXPECT_TRUE(destructed);
}

TEST(Callback, BorrowedCallable) {
  class Foo : public stout::enable_borrowable_from_this<Foo> {
   public:
    Foo(int i)
      : i_(i) {}

    auto Function() {
      return Borrow([this]() {
        return i_;
      });
    }

   private:
    int i_ = 0;
  };

  Foo foo(42);

  {
    Callback<int()> callback = foo.Function();

    EXPECT_EQ(foo.borrows(), 1);

    EXPECT_EQ(42, callback());

    EXPECT_EQ(foo.borrows(), 1);
  }

  EXPECT_EQ(foo.borrows(), 0);
}

} // namespace
} // namespace eventuals::test
