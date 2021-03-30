#pragma once

#include <string>
#include <vector>

#include "absl/types/optional.h"

#include "stout/grpc/client.h"
#include "stout/grpc/traits.h"

namespace stout {
namespace grpc {

class ClusterStatus
{
public:
  static ClusterStatus Ok()
  {
    return ClusterStatus();
  }

  static ClusterStatus Error(const std::string& error)
  {
    return ClusterStatus(error);
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
  ClusterStatus() {}
  ClusterStatus(const std::string& error) : error_(error) {}

  absl::optional<std::string> error_;
};


class Cluster
{
public:
  Cluster(
      std::initializer_list<std::string> targets,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials);

  ~Cluster();

  void Shutdown();

  void Wait();

  template <
    typename Service,
    typename Request,
    typename Response,
    typename Read,
    typename Finished>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ClientCall<Request, Response>, Response>::value
    && IsFinishedHandler<Finished, ClientCall<Request, Response>>::value,
    ClusterStatus> Broadcast(
        const std::string& name,
        const typename RequestResponseTraits::Details<Request>::Type* request,
        Read&& read,
        Finished&& finished)
  {
    return Broadcast<Request, Response, Read, Finished>(
        std::string(Service::service_full_name()) + "." + name,
        request,
        std::forward<Read>(read),
        std::forward<Finished>(finished));
  }

  template <
    typename Request,
    typename Response,
    typename Read,
    typename Finished>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ClientCall<Request, Response>, Response>::value
    && IsFinishedHandler<Finished, ClientCall<Request, Response>>::value,
    ClusterStatus> Broadcast(
        const std::string& name,
        const typename RequestResponseTraits::Details<Request>::Type* request,
        Read&& read,
        Finished&& finished)
  {
    ClientStatus status = ClientStatus::Ok();

    for (auto& client : clients_) {
      status = client->Call<Request, Response, Read, Finished>(
          name,
          request,
          Read(read), // NOTE: 'read' must be copyable!
          Finished(finished)); // NOTE: 'finished' must be copyable!

      if (!status.ok()) {
        break;
      }
    }

    return status.ok()
      ? ClusterStatus::Ok()
      : ClusterStatus::Error(status.error());
  }

  template <typename Service, typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ClientCall<Request, Response>, bool>::value,
    ClusterStatus> Broadcast(
        const std::string& name,
        Handler&& handler)
  {
    return Broadcast<Request, Response, Handler>(
        std::string(Service::service_full_name()) + "." + name,
        std::forward<Handler>(handler));
  }

  template <typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ClientCall<Request, Response>, bool>::value,
    ClusterStatus> Broadcast(
        const std::string& name,
        Handler&& handler)
  {
    ClientStatus status = ClientStatus::Ok();

    for (auto& client : clients_) {
      status = client->Call<Request, Response, Handler>(
          name,
          absl::nullopt,
          Handler(handler)); // NOTE: 'handler' must be copyable!

      if (!status.ok()) {
        break;
      }
    }

    return status.ok()
      ? ClusterStatus::Ok()
      : ClusterStatus::Error(status.error());
  }

private:
  std::vector<std::unique_ptr<Client>> clients_;
};

} // namespace grpc {
} // namespace stout {
