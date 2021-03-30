#include "stout/grpc/cluster.h"

namespace stout {
namespace grpc {

Cluster::Cluster(
    std::initializer_list<std::string> targets,
    const std::shared_ptr<::grpc::ChannelCredentials>& credentials)
{
  for (const auto& target : targets) {
    clients_.push_back(std::make_unique<Client>(target, credentials));
  }
}


Cluster::~Cluster()
{
  Shutdown();
  Wait();
}


void Cluster::Shutdown()
{
  for (auto& client : clients_) {
    client->Shutdown();
  }
}


void Cluster::Wait()
{
  for (auto& client : clients_) {
    client->Wait();
  }
  clients_.clear();
}

} // namespace grpc {
} // namespace stout {
