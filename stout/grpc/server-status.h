#pragma once

#include "absl/types/optional.h"

namespace stout {
namespace grpc {

class ServerStatus
{
public:
  static ServerStatus Ok()
  {
    return ServerStatus();
  }

  static ServerStatus Error(const std::string& error)
  {
    return ServerStatus(error);
  }

  bool ok() const
  {
    return !error_;
  }

  const std::string& error() const
  {
    return error_.value();
  }

private:
  ServerStatus() {}
  ServerStatus(const std::string& error) : error_(error) {}

  absl::optional<std::string> error_;
};

} // namespace grpc {
} // namespace stout {
