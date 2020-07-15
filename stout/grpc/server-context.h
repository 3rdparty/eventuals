#pragma once

#include "grpcpp/server_context.h"

#include "grpcpp/generic/async_generic_service.h"

#include "stout/notification.h"

namespace stout {
namespace grpc {

struct ServerContext
{    
  ServerContext()
    : stream_(&context_)
  {
    done_ = [this](bool, void*) {
      on_done_.Notify(context_.IsCancelled());
    };

    // NOTE: in the event that we shutdown the server and gRPC doesn't
    // give us a done notification as per the bug at:
    // https://github.com/grpc/grpc/issues/10136 (while the bug might
    // appear closed it's due to a bot rather than an actual fix) this
    // ServerContext instance will get deleted when the enclosing
    // Server gets deleted because it's stored in a std::unique_ptr
    // inside of the handler

    context_.AsyncNotifyWhenDone(&done_);
  }

  void OnDone(std::function<void(bool)>&& handler)
  {
    on_done_.Watch(std::move(handler));
  }

  ::grpc::GenericServerContext* context()
  {
    return &context_;
  }

  ::grpc::GenericServerAsyncReaderWriter* stream()
  {
    return &stream_;
  }

  std::string method() const
  {
    return context_.method();
  }

  std::string host() const
  {
    return context_.host();
  }

private:
  ::grpc::GenericServerContext context_;
  ::grpc::GenericServerAsyncReaderWriter stream_;

  std::function<void(bool, void*)> done_;
  Notification<bool> on_done_;
};


} // namespace grpc {
} // namespace stout {
