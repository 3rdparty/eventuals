#pragma once

namespace stout {
namespace grpc {

enum class CallType {
  UNARY,
  CLIENT_STREAMING,
  SERVER_STREAMING,
  BIDI_STREAMING,
};

} // namespace grpc {
} // namespace stout {

