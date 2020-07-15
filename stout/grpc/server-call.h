#pragma once

#include "stout/grpc/server-call-base.h"

namespace stout {
namespace grpc {

template <typename Request, typename Response>
class ServerCall : public ServerCallBase
{
public:
  ServerCall(std::unique_ptr<ServerContext>&& context)
    : ServerCallBase(std::move(context), CallType::UNARY)
  {}

  template <typename F>
  ServerCallStatus OnRead(F&& f)
  {
    return ServerCallBase::OnRead<Request>(
        [this, f = std::forward<F>(f)](auto&& request) mutable {
          f(this, std::forward<decltype(request)>(request));
        });
  }

  template <typename F>
  ServerCallStatus OnDone(F&& f)
  {
    return ServerCallBase::OnDone(
        [this, f = std::forward<F>(f)](bool cancelled) mutable {
          f(this, cancelled);
        });
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        options,
        std::forward<Callback>(callback),
        finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        options,
        std::function<void(bool)>(),
        finish_status);
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback),
        finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, finish_status);
  }
};


template <typename Request, typename Response>
class ServerCall<Stream<Request>, Response> : public ServerCallBase
{
public:
  ServerCall(std::unique_ptr<ServerContext>&& context)
    : ServerCallBase(std::move(context), CallType::CLIENT_STREAMING)
  {}

  template <typename F>
  ServerCallStatus OnRead(F&& f)
  {
    return ServerCallBase::OnRead<Request>(
        [this, f = std::forward<F>(f)](auto&& request) mutable {
          f(this, std::forward<decltype(request)>(request));
        });
  }

  template <typename F>
  ServerCallStatus OnDone(F&& f)
  {
    return ServerCallBase::OnDone(
        [this, f = std::forward<F>(f)](bool cancelled) mutable {
          f(this, cancelled);
        });
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        options,
        std::forward<Callback>(callback),
        finish_status);
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        std::forward<Callback>(callback),
        finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, options, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, finish_status);
  }
};


template <typename Request, typename Response>
class ServerCall<Request, Stream<Response>> : public ServerCallBase
{
public:
  ServerCall(std::unique_ptr<ServerContext>&& context)
    : ServerCallBase(std::move(context), CallType::SERVER_STREAMING)
  {}

  template <typename F>
  ServerCallStatus OnRead(F&& f)
  {
    return ServerCallBase::OnRead<Request>(
        [this, f = std::forward<F>(f)](auto&& request) mutable {
          f(this, std::forward<decltype(request)>(request));
        });
  }

  template <typename F>
  ServerCallStatus OnDone(F&& f)
  {
    return ServerCallBase::OnDone(
        [this, f = std::forward<F>(f)](bool cancelled) mutable {
          f(this, cancelled);
        });
  }

  template <typename Callback>
  ServerCallStatus Write(
      const Response& response,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions(),
      Callback&& callback = std::function<void(bool)>())
  {
    return ServerCallBase::Write(
        response,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus Write(
      const Response& response,
      Callback&& callback)
  {
    return ServerCallBase::Write(response, std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus WriteLast(
      const Response& response,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions(),
      Callback&& callback = std::function<void(bool)>())
  {
    return ServerCallBase::WriteLast(
        response,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus WriteLast(
      const Response& response,
      Callback&& callback)
  {
    return ServerCallBase::WriteLast(
        response,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        options,
        std::forward<Callback>(callback),
        finish_status);
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        std::forward<Callback>(callback),
        finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, options, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, finish_status);
  }
};


template <typename Request, typename Response>
class ServerCall<Stream<Request>, Stream<Response>> : public ServerCallBase
{
public:
  ServerCall(std::unique_ptr<ServerContext>&& context)
    : ServerCallBase(std::move(context), CallType::BIDI_STREAMING)
  {}

  template <typename F>
  ServerCallStatus OnRead(F&& f)
  {
    return ServerCallBase::OnRead<Request>(
        [this, f = std::forward<F>(f)](auto&& request) mutable {
          f(this, std::forward<decltype(request)>(request));
        });
  }

  template <typename F>
  ServerCallStatus OnDone(F&& f)
  {
    return ServerCallBase::OnDone(
        [this, f = std::forward<F>(f)](bool cancelled) mutable {
          f(this, cancelled);
        });
  }

  template <typename Callback>
  ServerCallStatus Write(
      const Response& response,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions(),
      Callback&& callback = std::function<void(bool)>())
  {
    return ServerCallBase::Write(
        response,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus Write(
      const Response& response,
      Callback&& callback)
  {
    return ServerCallBase::Write(response, std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus WriteLast(
      const Response& response,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions(),
      Callback&& callback = std::function<void(bool)>())
  {
    return ServerCallBase::WriteLast(
        response,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus WriteLast(
      const Response& response,
      Callback&& callback)
  {
    return ServerCallBase::WriteLast(
        response,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        options,
        std::forward<Callback>(callback),
        finish_status);
  }

  template <typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(
        response,
        std::forward<Callback>(callback),
        finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, options, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish(response, finish_status);
  }
};

} // namespace grpc {
} // namespace stout {
