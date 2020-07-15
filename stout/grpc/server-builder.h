#pragma once

#include <vector>

#include "absl/types/optional.h"

#include "grpcpp/server_builder.h"

#include "grpcpp/generic/async_generic_service.h"

#include "grpcpp/security/server_credentials.h"

#include "stout/grpc/server-status.h"

namespace stout {
namespace grpc {

// Forward declaration.
class Server;


struct ServerStatusOrServer
{
  ServerStatus status;
  std::unique_ptr<Server> server;
};


class ServerBuilder
{
public:
  ServerBuilder& SetNumberOfCompletionQueues(size_t n);

  // TODO(benh): Provide a 'setMaximumThreadsPerCompletionQueue' as well.
  ServerBuilder& SetMinimumThreadsPerCompletionQueue(size_t n);

  ServerBuilder& AddListeningPort(
      const std::string& address,
      std::shared_ptr<grpc_impl::ServerCredentials> credentials,
      int* selectedPort = nullptr);

  ServerStatusOrServer BuildAndStart();

private:
  ServerStatus status_ = ServerStatus::Ok();
  absl::optional<size_t> numberOfCompletionQueues_ = absl::nullopt;
  absl::optional<size_t> minimumThreadsPerCompletionQueue_ = absl::nullopt;
  std::vector<std::string> addresses_ = {};

  ::grpc::ServerBuilder builder_;

  std::unique_ptr<::grpc::AsyncGenericService> service_;
};

} // namespace grpc {
} // namespace stout {
