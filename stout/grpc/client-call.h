#pragma once

#include "stout/grpc/client-call-base.h"
#include "stout/grpc/traits.h"

namespace stout {
namespace grpc {

template <typename Request, typename Response>
class ClientCall : public ClientCallBase
{
public:
  ClientCall() : ClientCallBase(CallType::UNARY) {}

  template <typename F>
  ClientCallStatus OnRead(F&& f)
  {
    return ClientCallBase::OnRead<Response>(
        [this, f = std::forward<F>(f)](auto&& response) mutable {
          f(this, std::forward<decltype(response)>(response));
        });
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions())
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::function<void(bool)>());
  }

  template <typename F>
  ClientCallStatus OnFinished(F&& f)
  {
    return ClientCallBase::OnFinished(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  template <typename F>
  ClientCallStatus Finish(F&& f)
  {
    return ClientCallBase::Finish(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  using ClientCallBase::Finish;
};


template <typename Request, typename Response>
class ClientCall<Stream<Request>, Response> : public ClientCallBase
{
public:
  ClientCall() : ClientCallBase(CallType::CLIENT_STREAMING) {}

  template <typename F>
  ClientCallStatus OnRead(F&& f)
  {
    return ClientCallBase::OnRead<Response>(
        [this, f = std::forward<F>(f)](auto&& response) mutable {
          f(this, std::forward<decltype(response)>(response));
        });
  }

  template <typename Callback>
  ClientCallStatus Write(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    return ClientCallBase::Write(
        request,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ClientCallStatus Write(
      const Request& request,
      Callback&& callback)
  {
    return ClientCallBase::Write(
        request,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  ClientCallStatus Write(
      const Request& request)
  {
    return ClientCallBase::Write(
        request,
        ::grpc::WriteOptions(),
        std::function<void(bool)>());
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions())
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::function<void(bool)>());
  }

  using ClientCallBase::WritesDone;
  using ClientCallBase::WritesDoneAndFinish;

  template <typename F>
  ClientCallStatus OnFinished(F&& f)
  {
    return ClientCallBase::OnFinished(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  template <typename F>
  ClientCallStatus Finish(F&& f)
  {
    return ClientCallBase::Finish(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  using ClientCallBase::Finish;
};


template <typename Request, typename Response>
class ClientCall<Request, Stream<Response>> : public ClientCallBase
{
public:
  ClientCall() : ClientCallBase(CallType::SERVER_STREAMING) {}

  template <typename F>
  ClientCallStatus OnRead(F&& f)
  {
    return ClientCallBase::OnRead<Response>(
        [this, f = std::forward<F>(f)](auto&& response) mutable {
          f(this, std::forward<decltype(response)>(response));
        });
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions())
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::function<void(bool)>());
  }

  template <typename F>
  ClientCallStatus OnFinished(F&& f)
  {
    return ClientCallBase::OnFinished(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  template <typename F>
  ClientCallStatus Finish(F&& f)
  {
    return ClientCallBase::Finish(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  using ClientCallBase::Finish;
};


template <typename Request, typename Response>
class ClientCall<Stream<Request>, Stream<Response>> : public ClientCallBase
{
public:
  ClientCall() : ClientCallBase(CallType::BIDI_STREAMING) {}

  template <typename F>
  ClientCallStatus OnRead(F&& f)
  {
    return ClientCallBase::OnRead<Response>(
        [this, f = std::forward<F>(f)](auto&& response) mutable {
          f(this, std::forward<decltype(response)>(response));
        });
  }

  template <typename Callback>
  ClientCallStatus Write(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    return ClientCallBase::Write(
        request,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ClientCallStatus Write(
      const Request& request,
      Callback&& callback)
  {
    return ClientCallBase::Write(
        request,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  ClientCallStatus Write(
      const Request& request)
  {
    return ClientCallBase::Write(
        request,
        ::grpc::WriteOptions(),
        std::function<void(bool)>());
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::forward<Callback>(callback));
  }

  template <typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      Callback&& callback)
  {
    return ClientCallBase::WriteAndDone(
        request,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions())
  {
    return ClientCallBase::WriteAndDone(
        request,
        options,
        std::function<void(bool)>());
  }

  using ClientCallBase::WritesDone;
  using ClientCallBase::WritesDoneAndFinish;

  template <typename F>
  ClientCallStatus OnFinished(F&& f)
  {
    return ClientCallBase::OnFinished(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  template <typename F>
  ClientCallStatus Finish(F&& f)
  {
    return ClientCallBase::Finish(
        [this, f = std::forward<F>(f)](const ::grpc::Status& status) mutable {
          f(this, status);
        });
  }

  using ClientCallBase::Finish;
};

} // namespace grpc {
} // namespace stout {
