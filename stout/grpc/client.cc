#include "stout/grpc/client.h"

namespace stout {
namespace grpc {

Client::Client(
    const std::string& target,
    const std::shared_ptr<::grpc::ChannelCredentials>& credentials)
  : channel_(::grpc::CreateChannel(target, credentials)), stub_(channel_)
{
  // TODO(benh): Support more than one thread, e.g., there could be a
  // thread reading while there is a thread writing, or even multiple
  // threads processing responses concurrently.
  thread_ = std::thread(
      [cq = &cq_]() {
        void* tag = nullptr;
        bool ok = false;
        while (cq->Next(&tag, &ok)) {
          (*static_cast<std::function<void(bool, void*)>*>(tag))(ok, tag);
        }
      });
}


Client::~Client()
{
  Shutdown();
  Wait();
}


void Client::Shutdown()
{
  // Client might have been moved, use 'thread_' to distinguish.
  if (thread_.joinable()) {
    cq_.Shutdown();
  }
}


void Client::Wait()
{
  // Client might have been moved, use 'thread_' to distinguish.
  if (thread_.joinable()) {
    thread_.join();

    void* tag = nullptr;
    bool ok = false;
    while (cq_.Next(&tag, &ok)) {}
  }
}

} // namespace grpc {
} // namespace stout {
