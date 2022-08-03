#pragma once

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct TypeErasedStream {
  TypeErasedStream() = default;

  TypeErasedStream(const TypeErasedStream&) = default;
  TypeErasedStream(TypeErasedStream&&) noexcept = default;

  TypeErasedStream& operator=(const TypeErasedStream&) = default;
  TypeErasedStream& operator=(TypeErasedStream&&) noexcept = default;

  virtual ~TypeErasedStream() = default;

  virtual void Next() = 0;
  virtual void Done() = 0;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
