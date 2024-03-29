#pragma once

#include <memory>
#include <string>
#include <variant>

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// All errors should derive from 'Error' and be copyable
// since 'std::make_exception_ptr' works with a copy argument
// and there is no way to move error in it.
class Error : public std::exception {
 public:
  virtual ~Error() = default;

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

  ~RuntimeError() override = default;

  const char* what() const noexcept override {
    return what_arg_.c_str();
  }

 private:
  std::string what_arg_;
};

////////////////////////////////////////////////////////////////////////

class TypeErasedError final : public Error {
 public:
  template <
      typename E,
      std::enable_if_t<
          !std::is_same_v<std::decay_t<E>, TypeErasedError>,
          int> = 0>
  TypeErasedError(E&& e)
    : e_(std::make_shared<std::decay_t<E>>(std::forward<E>(e))) {
    static_assert(
        std::is_base_of_v<Error, std::decay_t<E>>,
        "Error must derive from 'Error'");
  }

  TypeErasedError(TypeErasedError&& that) = default;

  TypeErasedError(const TypeErasedError& that) = default;

  ~TypeErasedError() = default;

  const char* what() const noexcept override {
    return e_->what();
  }

 private:
  // Using 'std::shared_ptr' instead of 'std::unique_ptr' since
  // there is no copy constructor for 'std::unique_ptr' and 'TypeErasedError
  // extends 'Error'.
  std::shared_ptr<Error> e_;
};

template <typename... Errors>
inline std::string What(const std::variant<Errors...>& error) {
  return std::visit(
      [](const auto& error) {
        return error.what();
      },
      error);
}

////////////////////////////////////////////////////////////////////////

}; // namespace eventuals