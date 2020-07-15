#pragma once

namespace stout {
namespace grpc {

enum class ServerCallStatus {
  // Used for internal state as well as for return status.
  Ok,
  WritingLast,
  WaitingForFinish,
  Finishing,
  Done, // Either due to finishing or being cancelled.

  // Used only for return status.
  WritingUnavailable,
  OnReadCalledMoreThanOnce,
  FailedToSerializeResponse,
};

} // namespace grpc {
} // namespace stout {
