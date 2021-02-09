#include <thread>

#include "grpcpp/completion_queue.h"
#include "grpcpp/server.h"

#include "stout/grpc/server.h"
#include "stout/grpc/server-builder.h"

namespace stout {
namespace grpc {

ServerBuilder& ServerBuilder::SetNumberOfCompletionQueues(size_t n)
{
  if (numberOfCompletionQueues_) {
    const std::string error = "already set number of completion queues";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    numberOfCompletionQueues_ = n;
  }
  return *this;
}


// TODO(benh): Provide a 'setMaximumThreadsPerCompletionQueue' as well.
ServerBuilder& ServerBuilder::SetMinimumThreadsPerCompletionQueue(size_t n)
{
  if (minimumThreadsPerCompletionQueue_) {
    const std::string error = "already set minimum threads per completion queue";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    minimumThreadsPerCompletionQueue_ = n;
  }
  return *this;
}


ServerBuilder& ServerBuilder::AddListeningPort(
    const std::string& address,
    std::shared_ptr<::grpc::ServerCredentials> credentials,
    int* selectedPort)
{
  addresses_.push_back(address);
  builder_.AddListeningPort(address, credentials, selectedPort);
  return *this;
}


ServerStatusOrServer ServerBuilder::BuildAndStart()
{
  if (addresses_.empty()) {
    const std::string error = "no listening addresses specified";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  }

  if (!status_.ok()) {
    return ServerStatusOrServer {
      ServerStatus::Error("Error building server: " + status_.error()),
      nullptr
    };
  }

  service_ = absl::make_unique<::grpc::AsyncGenericService>();

  builder_.RegisterAsyncGenericService(service_.get());

  if (!numberOfCompletionQueues_) {
    numberOfCompletionQueues_ = 1;
  }

  if (!minimumThreadsPerCompletionQueue_) {
    minimumThreadsPerCompletionQueue_ = 1;
  }

  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs;

  for (size_t i = 0; i < numberOfCompletionQueues_.value(); ++i) {
    cqs.push_back(builder_.AddCompletionQueue());
  }

  std::unique_ptr<::grpc::Server> server = builder_.BuildAndStart();

  if (!server) {
    // TODO(benh): Are invalid addresses the only reason the server
    // wouldn't start? What about bad credentials?
    status_ = ServerStatus::Error("Error building server: invalid address(es)");

    return ServerStatusOrServer {
      status_,
      nullptr
    };
  } else {
    // NOTE: we wait to start the threads until after a succesful
    // 'BuildAndStart()' so that we don't have to bother with
    // stopping/joining.
    std::vector<std::thread> threads;
    for (auto&& cq : cqs) {
      for (size_t j = 0; j < minimumThreadsPerCompletionQueue_.value(); ++j) {
        threads.push_back(
            std::thread(
                [cq = cq.get()]() {
                  void* tag = nullptr;
                  bool ok = false;
                  while (cq->Next(&tag, &ok)) {
                    (*static_cast<std::function<void(bool, void*)>*>(tag))(ok, tag);
                  }
                }));
      }
    }

    return ServerStatusOrServer {
      ServerStatus::Ok(),
      std::unique_ptr<Server>(new Server(
          std::move(service_),
          std::move(server),
          std::move(cqs),
          std::move(threads)))
    };
  }
}

} // namespace grpc {
} // namespace stout {

