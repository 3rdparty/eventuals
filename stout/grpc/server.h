#pragma once

#include <thread>

#include "absl/container/flat_hash_map.h"

#include "absl/memory/memory.h"

#include "absl/synchronization/mutex.h"

#include "google/protobuf/descriptor.h"

#include "grpcpp/completion_queue.h"
#include "grpcpp/server.h"

#include "stout/notification.h"

#include "stout/grpc/logging.h"
#include "stout/grpc/traits.h"

#include "stout/grpc/server-builder.h"
#include "stout/grpc/server-call.h"
#include "stout/grpc/server-context.h"
#include "stout/grpc/server-status.h"

namespace stout {
namespace grpc {

// TODO(benh): Factor this out into a stout library.
template <typename F>
auto make_shared_function(F&& f)
{
  return [f = std::make_shared<std::decay_t<F>>(std::forward<F>(f))](
      auto&&... args) -> decltype(auto) {
    return (*f)(decltype(args)(args)...);
  };
}


class Server
{
public:
  ~Server();

  void Shutdown();

  void Wait();

  template <
    typename Service,
    typename Request,
    typename Response,
    typename Read,
    typename Done>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ServerCall<Request, Response>, Request>::value
    && IsDoneHandler<Done, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      Read&& read,
      Done&& done)
  {
    return Serve<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        std::string("*"),
        std::forward<Read>(read),
        std::forward<Done>(done));
  }

  template <
    typename Service,
    typename Request,
    typename Response,
    typename Read,
    typename Done>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ServerCall<Request, Response>, Request>::value
    && IsDoneHandler<Done, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      const std::string& host,
      Read&& read,
      Done&& done)
  {
    return Serve<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        host,
        [read = std::forward<Read>(read), done = std::forward<Done>(done)](auto&& call) {
          call->OnRead(std::move(read));
          call->OnDone(std::move(done));
        });
  }

  template <typename Request, typename Response, typename Read, typename Done>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ServerCall<Request, Response>, Request>::value
    && IsDoneHandler<Done, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      Read&& read,
      Done&& done)
  {
    return Serve<Request, Response>(
        name,
        std::string("*"),
        std::forward<Read>(read),
        std::forward<Done>(done));
  }

  template <typename Request, typename Response, typename Read, typename Done>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ServerCall<Request, Response>, Request>::value
    && IsDoneHandler<Done, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      const std::string& host,
      Read&& read,
      Done&& done)
  {
    return Serve<Request, Response>(
        name,
        host,
        [read = std::forward<Read>(read), done = std::forward<Done>(done)](auto&& call) {
          call->OnRead(std::move(read));
          call->OnDone(std::move(done));
        });
  }

  template <typename Service, typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      Handler&& handler)
  {
    return Serve<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        std::string("*"),
        std::forward<Handler>(handler));
  }

  template <typename Service, typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      const std::string& host,
      Handler&& handler)
  {
    return Serve<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        host,
        std::forward<Handler>(handler));
  }

  template <typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      Handler&& handler)
  {
    return Serve<Request, Response>(
        name,
        std::string("*"),
        std::forward<Handler>(handler));
  }

  template <typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ServerCall<Request, Response>>::value,
    ServerStatus> Serve(
      const std::string& name,
      const std::string& host,
      Handler&& handler);

private:
  friend class ServerBuilder;

  struct Endpoint
  {
    std::function<void(std::unique_ptr<ServerContext>&&)> serve;
  };

  Server(
      std::unique_ptr<::grpc::AsyncGenericService>&& service,
      std::unique_ptr<::grpc::Server>&& server,
      std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>>&& cqs,
      std::vector<std::thread>&& threads);

  void serve(std::unique_ptr<ServerContext>&& context);

  Endpoint* lookup(ServerContext* context);

  void unimplemented(ServerContext* context);

  absl::Mutex mutex_;

  std::unique_ptr<::grpc::AsyncGenericService> service_;
  std::unique_ptr<::grpc::Server> server_;
  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs_;
  std::vector<std::thread> threads_;

  std::vector<std::function<void(bool, void*)>> handlers_;

  std::function<void(bool, void*)> noop = [](bool, void*) {};

  absl::flat_hash_map<std::pair<std::string, std::string>, std::unique_ptr<Endpoint>> endpoints_;
};


template <typename Request, typename Response, typename Handler>
std::enable_if_t<
  IsMessage<Request>::value
  && IsMessage<Response>::value
  && IsCallHandler<Handler, ServerCall<Request, Response>>::value,
  ServerStatus> Server::Serve(
      const std::string& name,
      const std::string& host,
      Handler&& handler)
{
  const auto* method = google::protobuf::DescriptorPool::generated_pool()
    ->FindMethodByName(name);

  if (method == nullptr) {
    return ServerStatus::Error("Method not found");
  }

  auto error = RequestResponseTraits::Validate<Request, Response>(method);

  if (error) {
    return ServerStatus::Error(error->message);
  }

  std::string path = "/" + name;

  size_t index = path.find_last_of(".");

  path.replace(index, 1, "/");

  auto endpoint = absl::make_unique<Endpoint>();

  endpoint->serve = [handler = std::forward<Handler>(handler)](
      std::unique_ptr<ServerContext>&& context) {
    // NOTE: we use a std::unique_ptr with a custom deleter so we can
    // handle proper cleanup because the gRPC subsystem needs the
    // server context and stream (stored in 'context') to still be
    // present for finishing.
    auto deleter = [](auto* call) {
      // NOTE: using private 'OnDoneDoneDone()' which gets invoked
      // *after* all of the other 'OnDone()' handlers.
      call->OnDoneDoneDone([call](bool) {
        delete call;
      });
    };

    auto call = std::unique_ptr<ServerCall<Request, Response>, decltype(deleter)>(
        // TODO(benh): Provide an allocator for calls.
        new ServerCall<Request, Response>(std::move(context)),
        std::move(deleter));

    handler(std::move(call));
  };

  mutex_.Lock();

  if (!endpoints_.try_emplace(std::make_pair(path, host), std::move(endpoint)).second) {
    mutex_.Unlock();
    return ServerStatus::Error("Already serving " + name + " for host " + host);
  }

  mutex_.Unlock();

  return ServerStatus::Ok();
}
  
} // namespace grpc {
} // namespace stout {
