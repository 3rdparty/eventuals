#pragma once

#include <utility>

// See the 'Contributor Guide' in README.md for documentation about
// how to create your own compile-time "builder".

////////////////////////////////////////////////////////////////////////

namespace builder {

////////////////////////////////////////////////////////////////////////

// Helper type for "builder" fields.
template <typename Value_, bool has_>
class Field;

template <typename Value_>
class Field<Value_, false> {
 public:
  virtual ~Field() = default;

  template <typename... Args>
  [[nodiscard]] auto Set(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "'Set' expects at least 1 argument");
    return Field<Value_, true>{Value_{std::forward<Args>(args)...}};
  }
};

template <typename Value_>
class Field<Value_, true> {
 public:
  Field(Value_ value)
    : value_(std::move(value)) {}

  virtual ~Field() = default;

  Value_& value() & {
    return value_;
  }

  const Value_& value() const& {
    return value_;
  }

  Value_&& value() && {
    return std::move(value_);
  }

  const Value_&& value() const&& {
    return std::move(value_);
  }

  const Value_* operator->() const {
    return &value_;
  }

  Value_* operator->() {
    return &value_;
  }

 private:
  Value_ value_;
};

////////////////////////////////////////////////////////////////////////

template <typename Value_, bool has_>
class FieldWithDefault;

template <typename Value_>
class FieldWithDefault<Value_, false> final : public Field<Value_, false> {
 public:
  template <
      typename Value,
      std::enable_if_t<std::is_convertible_v<Value, Value_>, int> = 0>
  FieldWithDefault(Value&& value)
    : default_(std::forward<Value>(value)) {}

  FieldWithDefault(FieldWithDefault&&) noexcept = default;

  ~FieldWithDefault() override = default;

  template <typename... Args>
  [[nodiscard]] auto Set(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "'Set' expects at least 1 argument");
    return FieldWithDefault<Value_, true>{Value_{std::forward<Args>(args)...}};
  }

  Value_& value() & {
    return default_;
  }

  const Value_& value() const& {
    return default_;
  }

  Value_&& value() && {
    return std::move(default_);
  }

  const Value_&& value() const&& {
    return std::move(default_);
  }

  const Value_* operator->() const {
    return &default_;
  }

  Value_* operator->() {
    return &default_;
  }

 private:
  Value_ default_;
};

template <typename Value_>
class FieldWithDefault<Value_, true> final : public Field<Value_, true> {
 public:
  template <
      typename Value,
      std::enable_if_t<std::is_convertible_v<Value, Value_>, int> = 0>
  FieldWithDefault(Value&& value)
    : Field<Value_, true>(std::forward<Value>(value)) {}

  FieldWithDefault(FieldWithDefault&&) noexcept = default;

  ~FieldWithDefault() override = default;
};

////////////////////////////////////////////////////////////////////////

template <typename Value_, bool has_>
class RepeatedField;

template <typename Value_>
class RepeatedField<Value_, false> final : public Field<Value_, false> {
 public:
  template <
      typename Value,
      std::enable_if_t<std::is_convertible_v<Value, Value_>, int> = 0>
  RepeatedField(Value&& value)
    : default_(std::forward<Value>(value)) {}

  RepeatedField(RepeatedField&&) noexcept = default;

  ~RepeatedField() override = default;

  template <typename... Args>
  [[nodiscard]] auto Set(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "'Set' expects at least 1 argument");
    return RepeatedField<Value_, true>{Value_{std::forward<Args>(args)...}};
  }

  Value_& value() & {
    return default_;
  }

  const Value_& value() const& {
    return default_;
  }

  Value_&& value() && {
    return std::move(default_);
  }

  const Value_&& value() const&& {
    return std::move(default_);
  }

  const Value_* operator->() const {
    return &default_;
  }

  Value_* operator->() {
    return &default_;
  }

 private:
  Value_ default_;
};

template <typename Value_>
class RepeatedField<Value_, true> final : public Field<Value_, true> {
 public:
  template <
      typename Value,
      std::enable_if_t<std::is_convertible_v<Value, Value_>, int> = 0>
  RepeatedField(Value&& value)
    : Field<Value_, true>(std::forward<Value>(value)) {}

  RepeatedField(RepeatedField&&) noexcept = default;

  ~RepeatedField() override = default;

  template <typename... Args>
  [[nodiscard]] auto Set(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "'Set' expects at least 1 argument");
    return RepeatedField<Value_, true>{Value_{std::forward<Args>(args)...}};
  }
};

////////////////////////////////////////////////////////////////////////

class Builder {
 public:
  virtual ~Builder() = default;

 protected:
  // Helper that creates a "builder" by calling it's constructor with a
  // set of "fields". The "builder" is parameterized by the list of
  // "has" values representing which of the fields have been set and
  // which have not.
  template <
      template <bool...>
      class Builder,
      template <typename, bool>
      class... Fields,
      typename... Values,
      bool... has>
  [[nodiscard]] auto Construct(Fields<Values, has>... fields) {
    return Builder<has...>(std::move(fields)...);
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace builder

////////////////////////////////////////////////////////////////////////
