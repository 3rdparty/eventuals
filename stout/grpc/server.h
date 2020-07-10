#pragma once

#include <thread>

#include "absl/base/call_once.h"

#include "absl/container/flat_hash_map.h"

#include "absl/memory/memory.h"

#include "absl/synchronization/mutex.h"

#include "absl/types/optional.h"

#include "google/protobuf/descriptor.h"

#include "grpcpp/generic/async_generic_service.h"

#include "grpc/grpc.h"

#include "grpcpp/completion_queue.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"

#include "grpcpp/security/server_credentials.h"

#include "stout/borrowed_ptr.h"
#include "stout/notification.h"

#include "stout/grpc/call-base.h"
#include "stout/grpc/call-type.h"
#include "stout/grpc/logging.h"
#include "stout/grpc/traits.h"

namespace stout {
namespace grpc {

// Forward declaration.
template <typename Request, typename Response>
class ServerCall;


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


class ServerStatus
{
public:
  static ServerStatus Ok()
  {
    return ServerStatus();
  }

  static ServerStatus Error(const std::string& error)
  {
    return ServerStatus(error);
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
  ServerStatus() {}
  ServerStatus(const std::string& error) : error_(error) {}

  absl::optional<std::string> error_;
};


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
  ~Server()
  {
    Shutdown();
    Wait();
  }

  void Shutdown()
  {
    server_->Shutdown();

    for (auto&& cq : cqs_) {
      cq->Shutdown();
    }
  }

  void Wait()
  {
    for (auto&& thread : threads_) {
      thread.join();
    }

    for (auto&& cq : cqs_) {
      void* tag = nullptr;
      bool ok = false;
      while (cq->Next(&tag, &ok)) {}
    }
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
      Read&& read,
      Done&& done)
  {
    return Serve<Service, Request, Response, Read, Done>(
        name,
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
    return Serve<Request, Response, Read, Done>(
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
    return Serve<Service, Request, Response, Handler>(
        name,
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
    return Serve<Request, Response, Handler>(
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
    return Serve<Request, Response, Handler>(
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
      // won't have until after we've created the handler (a "chicken
      // or the egg" scenario).
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

  void serve(std::unique_ptr<ServerContext>&& context)
  {
    auto* endpoint = lookup(context.get());
    if (endpoint != nullptr) {
      endpoint->serve(std::move(context));
    } else {
      unimplemented(context.release());
    }
  }

  Endpoint* lookup(ServerContext* context)
  {
    Endpoint* endpoint = nullptr;

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

  void unimplemented(ServerContext* context)
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

  absl::Mutex mutex_;

  std::unique_ptr<::grpc::AsyncGenericService> service_;
  std::unique_ptr<::grpc::Server> server_;
  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs_;
  std::vector<std::thread> threads_;

  std::vector<std::function<void(bool, void*)>> handlers_;

  std::function<void(bool, void*)> noop = [](bool, void*) {};

  absl::flat_hash_map<std::pair<std::string, std::string>, std::unique_ptr<Endpoint>> endpoints_;
};


struct ServerStatusOrServer
{
  ServerStatus status;
  std::unique_ptr<Server> server;
};


class ServerBuilder
{
public:
  ServerBuilder& SetNumberOfCompletionQueues(size_t n)
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
  ServerBuilder& SetMinimumThreadsPerCompletionQueue(size_t n)
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

  ServerBuilder& AddListeningPort(
      const std::string& address,
      std::shared_ptr<grpc_impl::ServerCredentials> credentials,
      int* selectedPort = nullptr)
  {
    addresses_.push_back(address);
    builder_.AddListeningPort(address, credentials, selectedPort);
    return *this;
  }

  ServerStatusOrServer BuildAndStart()
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
      // TODO(benh): Are invalid addresses the only reason the
      // server wouldn't start? What about bad credentials?
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

private:
  ServerStatus status_ = ServerStatus::Ok();
  absl::optional<size_t> numberOfCompletionQueues_ = absl::nullopt;
  absl::optional<size_t> minimumThreadsPerCompletionQueue_ = absl::nullopt;
  std::vector<std::string> addresses_ = {};

  ::grpc::ServerBuilder builder_;

  std::unique_ptr<::grpc::AsyncGenericService> service_;
};


enum class ServerCallStatus {
  // Used for internal state as well as for return status.
  Ok,
  WritingLast,
  WaitingForFinish,
  Finishing,
  Done, // Either due to finishing or being cancelled.

  // Used only for return status.
  WritingUnavailable,
  OnReadCalledMoreThanOnce,
  FailedToSerializeResponse,
};


class ServerCallBase : public CallBase
{
public:
  ServerCallBase(std::unique_ptr<ServerContext>&& context, CallType type);

  ::grpc::GenericServerContext* context()
  {
    return context_->context();
  }

  ServerCallStatus Finish(const ::grpc::Status& finish_status);

protected:
  template <typename Response>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      ::grpc::WriteOptions options,
      const ::grpc::Status& finish_status)
  {
    ::grpc::ByteBuffer buffer;

    if (!serialize(response, &buffer)) {
      return ServerCallStatus::FailedToSerializeResponse;
    }

    // NOTE: invariant here is that since 'Write' and 'WriteLast' are
    // *not* exposed via ServerCall then we can use 'WriteAndFinish' here
    // because no other writes should be outstanding.
    if (type_ != CallType::SERVER_STREAMING || type_ != CallType::BIDI_STREAMING) {
      auto status = ServerCallStatus::Ok;
      mutex_.Lock();
      if (status_ == ServerCallStatus::Ok) {
        status_ = ServerCallStatus::Finishing;
        mutex_.Unlock();
        stream()->WriteAndFinish(buffer, options, finish_status, &finish_callback_);
        return ServerCallStatus::Ok;
      } else {
        status = status_;
        mutex_.Unlock();
        return status;
      }
    } else {
      absl::MutexLock lock(&mutex_);

      if (status_ == ServerCallStatus::Ok) {
        status_ = ServerCallStatus::WritingLast;
        auto status = write(&buffer, options);
        if (status == ServerCallStatus::Ok) {
          finish_status_ = finish_status;
          status_ = ServerCallStatus::Finishing;
        }
        return status;
      }

      return status_;
    }
  }

  template <typename Response>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return WriteAndFinish(response, ::grpc::WriteOptions(), finish_status);
  }

  template <typename F>
  ServerCallStatus OnDone(F&& f)
  {
    done_.Watch(std::forward<F>(f));
    return ServerCallStatus::Ok;
  }

  template <typename Request, typename F>
  ServerCallStatus OnRead(F&& f)
  {
    mutex_.Lock();

    auto status = status_;

    mutex_.Unlock();

    if (status != ServerCallStatus::Ok) {
      return status;
    }

    status = ServerCallStatus::OnReadCalledMoreThanOnce;

    // NOTE: we set read callback here vs constructor so we can capture 'f'.
    absl::call_once(read_once_, [this, &f, &status]() {
      read_callback_ = [this, f = std::forward<F>(f)](bool ok, void*) mutable {
        if (ok) {
          // TODO(benh): Provide an allocator for requests.
          std::unique_ptr<Request> request(new Request());
          if (deserialize(&read_buffer_, request.get())) {
            f(std::move(request));
          }
          // Keep reading if the client is streaming.
          if (type_ == CallType::CLIENT_STREAMING
              || type_ == CallType::BIDI_STREAMING) {
            stream()->Read(&read_buffer_, &read_callback_);
          }
        } else {
          // Signify end of stream if the client is streaming.
          if (type_ == CallType::CLIENT_STREAMING
              || type_ == CallType::BIDI_STREAMING) {
            f(std::unique_ptr<Request>(nullptr));
          }
        }
      };

      stream()->Read(&read_buffer_, &read_callback_);

      status = ServerCallStatus::Ok;
    });

    return status;
  }

  template <typename Response>
  ServerCallStatus Write(
      const Response& response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions())
  {
    ::grpc::ByteBuffer buffer;

    if (!serialize(response, &buffer)) {
      return ServerCallStatus::FailedToSerializeResponse;
    }

    absl::MutexLock lock(&mutex_);

    if (status_ != ServerCallStatus::Ok) {
      return status_;
    }

    return write(&buffer, options);
  }

  template <typename Response>
  ServerCallStatus WriteLast(
      const Response& response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions())
  {
    absl::MutexLock lock(&mutex_);

    if (status_ == ServerCallStatus::Ok) {
      status_ = ServerCallStatus::WritingLast;
      auto status = write(response, options);
      if (status == ServerCallStatus::Ok) {
        status_ = ServerCallStatus::WaitingForFinish;
      }
      return status;
    }

    return status_;
  }

private:
  ServerCallStatus write(
      ::grpc::ByteBuffer* buffer,
      ::grpc::WriteOptions options)
    EXCLUSIVE_LOCKS_REQUIRED(mutex_)
  {
    if (!write_callback_) {
      return ServerCallStatus::WritingUnavailable;
    }

    write_buffers_.emplace_back();

    write_buffers_.back().first.Swap(buffer);
    write_buffers_.back().second = options;

    if (write_buffers_.size() == 1) {
      buffer = &write_buffers_.front().first;
    } else {
      buffer = nullptr;
    }

    mutex_.Unlock();

    if (buffer != nullptr) {
      stream()->Write(*buffer, options, &write_callback_);
    }

    mutex_.Lock();

    return ServerCallStatus::Ok;
  }

  ::grpc::GenericServerAsyncReaderWriter* stream()
  {
    return context_->stream();
  }

  absl::Mutex mutex_;

  ServerCallStatus status_;

  absl::once_flag read_once_;
  std::function<void(bool, void*)> read_callback_;
  ::grpc::ByteBuffer read_buffer_;

  std::function<void(bool, void*)> write_callback_;
  std::list<
    std::pair<
      ::grpc::ByteBuffer,
      ::grpc::WriteOptions>> write_buffers_;

  std::function<void(bool, void*)> finish_callback_;
  absl::optional<::grpc::Status> finish_status_ = absl::nullopt;

  std::unique_ptr<ServerContext> context_;

  Notification<bool> done_;

  CallType type_;
};


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

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      ::grpc::WriteOptions options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, options, finish_status);
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

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      ::grpc::WriteOptions options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, options, finish_status);
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

  ServerCallStatus Write(
      const Response& response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions())
  {
    return ServerCallBase::Write<Response>(response, options);
  }

  ServerCallStatus WriteLast(
      const Response& response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions())
  {
    return ServerCallBase::WriteLast<Response>(response, options);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      ::grpc::WriteOptions options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, options, finish_status);
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

  ServerCallStatus Write(
      const Response& response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions())
  {
    return ServerCallBase::Write<Response>(response, options);
  }

  ServerCallStatus WriteLast(
      const Response& response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions())
  {
    return ServerCallBase::WriteLast<Response>(response, options);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, finish_status);
  }

  ServerCallStatus WriteAndFinish(
      const Response& response,
      ::grpc::WriteOptions options,
      const ::grpc::Status& finish_status)
  {
    return ServerCallBase::WriteAndFinish<Response>(response, options, finish_status);
  }
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
    // NOTE: we let the handler "borrow" the call because we need it
    // to stick around until 'OnDone' because the gRPC subsystem needs
    // the server context and stream (stored in 'context'). Said
    // another way, if we passed a std::unique_ptr then we also need
    // to have the client keep that around until an 'OnDone' call and
    // requiring everyone to do that is unnecessary copying, so we
    // give them a borrowed_ptr and handle the delete for them!
    //
    // TODO(benh): Provide an allocator for calls.
    auto call = borrow(
        new ServerCall<Request, Response>(std::move(context)),
        [](auto* call) {
          // NOTE: see note in ServerCallBase constructor as to the
          // invariant that we must set up our 'OnDone' handler
          // *after* the ServerCallBase constructor.
          call->OnDone([](auto* call, bool) {
            delete call;
          });
        });

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
