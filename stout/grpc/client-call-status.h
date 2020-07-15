#pragma once

#include <string>

namespace stout {
namespace grpc {

enum class ClientCallStatus {
  // Used for internal state as well as for return status.
  Ok,
  WaitingForFinish,
  Finishing,
  Finished,

  // Used only for return status.
  WritingUnavailable, // Call most likely dead.
  OnReadCalledMoreThanOnce,
  OnFinishedCalledMoreThanOnce,
  FailedToSerializeRequest,
};


inline std::string stringify(ClientCallStatus status)
{
  switch (status) {
    case ClientCallStatus::Ok:
      return "Ok";
    case ClientCallStatus::WaitingForFinish:
      return "WaitingForFinish";
    case ClientCallStatus::Finishing:
      return "Finishing";
    case ClientCallStatus::Finished:
      return "Finished";
    case ClientCallStatus::WritingUnavailable:
      return "WritingUnavailable";
    case ClientCallStatus::OnReadCalledMoreThanOnce:
      return "OnReadCalledMoreThanOnce";
    case ClientCallStatus::OnFinishedCalledMoreThanOnce:
      return "OnFinishedCalledMoreThanOnce";
    case ClientCallStatus::FailedToSerializeRequest:
      return "FailedToSerializeRequest";
  }
}

} // namespace grpc {
} // namespace stout {
