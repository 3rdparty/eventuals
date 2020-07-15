#include "stout/grpc/server.h"

namespace stout {
namespace grpc {

Server::Server(
    std::unique_ptr<::grpc::AsyncGenericService>&& service,
    std::unique_ptr<::grpc::Server>&& server,
    std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>>&& cqs,
    std::vector<std::thread>&& threads)
  : service_(std::move(service)),
    server_(std::move(server)),
    cqs_(std::move(cqs)),
    threads_(std::move(threads))
{
  for (auto&& cq : cqs_) {
    // Create a context and handler for every completion queue and
    // start the "inifinite loop" of getting callbacks from gRPC for
    // each new call.
    //
    // NOTE: for this first context we keep a pointer so that we can
    // set the pointer to the handler stored in handlers_ which we
    // won't have until after we've created the handler (a "chicken or
    // the egg" scenario).
    auto* context = new ServerContext();

    handlers_.push_back(
        make_shared_function(
            [this,
             cq = cq.get(),
             context = std::unique_ptr<ServerContext>(context)](
                 bool ok, void* tag) mutable {
              if (ok) {
                // Stash the received context so we can make a new
                // context to pass to gRPC before we start to serve.
                std::unique_ptr<ServerContext> received = std::move(context);
                context = absl::make_unique<ServerContext>();

                service_->RequestCall(
                    context->context(),
                    context->stream(),
                    cq,
                    cq,
                    tag);

                serve(std::move(received));
              }
            }));

    service_->RequestCall(
        context->context(),
        context->stream(),
        cq.get(),
        cq.get(),
        &handlers_.back());
  }
}


Server::~Server()
{
  Shutdown();
  Wait();
}


void Server::Shutdown()
{
  // Server might have gotten moved.
  if (server_) {
    server_->Shutdown();
  }

  for (auto&& cq : cqs_) {
    cq->Shutdown();
  }
}


void Server::Wait()
{
  if (server_) {
    server_->Wait();
  }

  for (auto&& thread : threads_) {
    thread.join();
  }

  for (auto&& cq : cqs_) {
    void* tag = nullptr;
    bool ok = false;
    while (cq->Next(&tag, &ok)) {}
  }
}


void Server::serve(std::unique_ptr<ServerContext>&& context)
{
  auto* endpoint = lookup(context.get());
  if (endpoint != nullptr) {
    endpoint->serve(std::move(context));
  } else {
    unimplemented(context.release());
  }
}


Server::Endpoint* Server::lookup(ServerContext* context)
{
  Server::Endpoint* endpoint = nullptr;

  mutex_.ReaderLock();

  auto iterator = endpoints_.find(
      std::make_pair(context->method(), context->host()));

  if (iterator != endpoints_.end()) {
    endpoint = iterator->second.get();
  } else {
    iterator = endpoints_.find(
        std::make_pair(context->method(), "*"));

    if (iterator != endpoints_.end()) {
      endpoint = iterator->second.get();
    }
  }

  mutex_.ReaderUnlock();

  return endpoint;
}


void Server::unimplemented(ServerContext* context)
{
  VLOG_IF(1, STOUT_GRPC_LOG)
    << "Dropping " << context->method()
    << " for host " << context->host() << std::endl;

  auto status = ::grpc::Status(
      ::grpc::UNIMPLEMENTED,
      context->method() + " for host " + context->host());

  context->stream()->Finish(status, &noop);

  context->OnDone([context](bool) {
    delete context;
  });
}

} // namespace grpc {
} // namespace stout {
