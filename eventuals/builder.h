#pragma once

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

  template <typename Value>
  auto Set(Value&& value) {
    static_assert(std::is_convertible_v<Value, Value_>);
    return Field<Value_, true>{std::forward<Value>(value)};
  }
};

template <typename Value_>
class Field<Value_, true> {
 public:
  Field(Value_ value)
    : value_(std::move(value)) {}

  virtual ~Field() = default;

  auto& value() & {
    return value_;
  }

  const auto& value() const& {
    return value_;
  }

  auto&& value() && {
    return std::move(value_);
  }

  const auto&& value() const&& {
    return std::move(value_);
  }

  const auto* operator->() const {
    return &value_;
  }

  auto* operator->() {
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

  FieldWithDefault(FieldWithDefault&&) = default;

  ~FieldWithDefault() override = default;

  template <typename Value>
  auto Set(Value&& value) {
    static_assert(std::is_convertible_v<Value, Value_>);
    return FieldWithDefault<Value_, true>{std::forward<Value>(value)};
  }

  auto& value() & {
    return default_;
  }

  const auto& value() const& {
    return default_;
  }

  auto&& value() && {
    return std::move(default_);
  }

  const auto&& value() const&& {
    return std::move(default_);
  }

  const auto* operator->() const {
    return &default_;
  }

  auto* operator->() {
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

  FieldWithDefault(FieldWithDefault&&) = default;

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

  RepeatedField(RepeatedField&&) = default;

  ~RepeatedField() override = default;

  template <typename Value>
  auto Set(Value&& value) {
    static_assert(std::is_convertible_v<Value, Value_>);
    return RepeatedField<Value_, true>{std::forward<Value>(value)};
  }

  auto& value() & {
    return default_;
  }

  const auto& value() const& {
    return default_;
  }

  auto&& value() && {
    return std::move(default_);
  }

  const auto&& value() const&& {
    return std::move(default_);
  }

  const auto* operator->() const {
    return &default_;
  }

  auto* operator->() {
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

  RepeatedField(RepeatedField&&) = default;

  ~RepeatedField() override = default;

  template <typename Value>
  auto Set(Value&& value) {
    static_assert(std::is_convertible_v<Value, Value_>);
    return RepeatedField<Value_, true>{std::forward<Value>(value)};
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
  auto Construct(Fields<Values, has>... fields) {
    return Builder<has...>(std::move(fields)...);
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace builder

////////////////////////////////////////////////////////////////////////
