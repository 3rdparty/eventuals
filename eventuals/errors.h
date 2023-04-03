#pragma once

#include <memory>
#include <string>

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class Error {
 public:
  virtual ~Error() = default;

  virtual std::string what() const noexcept = 0;

 protected:
  Error() = default;
};

////////////////////////////////////////////////////////////////////////

class RuntimeError final : public Error {
 public:
  RuntimeError(const std::string& what_arg)
    : what_arg_(what_arg) {}

  RuntimeError(const char* what_arg)
    : what_arg_(what_arg) {}

  RuntimeError(std::string&& what_arg)
    : what_arg_(std::move(what_arg)) {}

  RuntimeError(const RuntimeError& that)
    : what_arg_(that.what()) {}

  RuntimeError(RuntimeError&& that)
    : what_arg_(std::move(that.what_arg_)) {}

  RuntimeError& operator=(const RuntimeError& that) {
    what_arg_ = that.what_arg_;

    return *this;
  }

  RuntimeError& operator=(RuntimeError&& that) {
    what_arg_ = std::move(that.what_arg_);

    return *this;
  }

  std::string what() const noexcept override {
    return what_arg_;
  }

 private:
  std::string what_arg_;
};

////////////////////////////////////////////////////////////////////////

class TypeErasedError final : public Error {
 public:
  template <typename E>
  TypeErasedError(E&& e)
    : e_(std::make_unique<std::remove_reference_t<E>>(std::forward<E>(e))) {
    static_assert(
        std::is_base_of_v<Error, std::decay_t<E>>,
        "Error must derive from 'Error'");
  }

  std::string what() const noexcept override {
    return e_->what();
  }

 private:
  std::unique_ptr<Error> e_;
};

////////////////////////////////////////////////////////////////////////

}; // namespace eventuals