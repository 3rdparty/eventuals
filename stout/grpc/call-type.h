#pragma once

namespace stout {
namespace eventuals {
namespace grpc {

enum class CallType {
  UNARY,
  CLIENT_STREAMING,
  SERVER_STREAMING,
  BIDI_STREAMING,
};

} // namespace grpc
} // namespace eventuals
} // namespace stout
