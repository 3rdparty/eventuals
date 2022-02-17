#pragma once

#include <cassert>
#include <deque>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "eventuals/catch.h"
#include "eventuals/eventual.h"
#include "eventuals/grpc/logging.h"
#include "eventuals/grpc/server.h"
#include "eventuals/grpc/traits.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/pipe.h"
#include "eventuals/repeat.h"
#include "eventuals/task.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "google/protobuf/descriptor.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/generic/async_generic_service.h"
#include "grpcpp/impl/codegen/proto_utils.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "stout/notification.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

using eventuals::operator|;

////////////////////////////////////////////////////////////////////////

// Forward declaration.
class Server;

////////////////////////////////////////////////////////////////////////

class Service {
 public:
  virtual ~Service() = default;

  virtual Task::Of<void> Serve() = 0;

  virtual char const* name() = 0;

  void Register(Server* server) {
    server_ = server;
  }

 protected:
  Server& server() {
    return *CHECK_NOTNULL(server_);
  }

 private:
  Server* server_;
};

////////////////////////////////////////////////////////////////////////

struct ServerContext {
  ServerContext()
    : stream_(&context_) {
    // NOTE: according to documentation we must set up the done
    // callback _before_ we start using the context. Thus we use a
    // 'Notification' in order to actually queue the callback later,
    // which also gives us the added benefit of having more than once
    // callback.
    done_callback_ = [this](bool) {
      done_.Notify(context_.IsCancelled());
    };

    context_.AsyncNotifyWhenDone(&done_callback_);

    // NOTE: it's possible that after doing a shutdown of the server
    // gRPC won't give us a done notification as per the bug at:
    // https://github.com/grpc/grpc/issues/10136 (while the bug might
    // appear closed it's due to a bot rather than an actual fix)
    // which can lead to memory or resource leaks if any callbacks set
    // up don't get executed.
  }

  void OnDone(std::function<void(bool)>&& f) {
    done_.Watch(std::move(f));
  }

  // Performs 'Finish()' then 'OnDone()' in sequence to overcome the
  // non-deterministic ordering of the finish and done callbacks that
  // grpc introduces.
  //
  // NOTE: it's remarkably surprising behavior that grpc will invoke
  // the finish callback _after_ the done callback!!!!!! This function
  // lets you get around that by sequencing the two callbacks.
  void FinishThenOnDone(::grpc::Status status, std::function<void(bool)>&& f) {
    CHECK(!finish_callback_)
        << "attempted to call FinishThenOnDone more than once";

    finish_on_done_ = std::move(f);

    finish_callback_ = [this](bool) {
      OnDone(std::move(finish_on_done_));
    };

    EVENTUALS_GRPC_LOG(1)
        << "Finishing call (" << this << ") for "
        << "host = " << host() << " and "
        << "path = " << method();

    stream_.Finish(status, &finish_callback_);
  }

  ::grpc::GenericServerContext* context() {
    return &context_;
  }

  ::grpc::GenericServerAsyncReaderWriter* stream() {
    return &stream_;
  }

  std::string method() const {
    return context_.method();
  }

  std::string host() const {
    return context_.host();
  }

 private:
  ::grpc::GenericServerContext context_;
  ::grpc::GenericServerAsyncReaderWriter stream_;

  Callback<bool> done_callback_;
  Callback<bool> finish_callback_;

  std::function<void(bool)> finish_on_done_;

  stout::Notification<bool> done_;
};

////////////////////////////////////////////////////////////////////////

// 'ServerReader' abstraction acts like the synchronous
// '::grpc::ServerReader' but instead of a blocking 'Read()' call we
// return a stream!
template <typename RequestType_>
class ServerReader {
 public:
  // TODO(benh): borrow 'context' or the enclosing 'ServerCall' so
  // that we ensure that it doesn't get destructed while our
  // eventuals are still outstanding.
  ServerReader(ServerContext* context)
    : context_(context) {}

  auto Read() {
    struct Data {
      ServerReader* reader = nullptr;
      ::grpc::ByteBuffer buffer;
      void* k = nullptr;
    };
    return eventuals::Stream<RequestType_>()
        .next([this,
               data = Data{},
               callback = Callback<bool>()](auto& k) mutable {
          using K = std::decay_t<decltype(k)>;

          if (!callback) {
            data.reader = this;
            data.k = &k;
            callback = [&data](bool ok) mutable {
              auto& k = *reinterpret_cast<K*>(data.k);
              if (ok) {
                RequestType_ request;
                if (deserialize(&data.buffer, &request)) {
                  EVENTUALS_GRPC_LOG(1)
                      << "Received request for call ("
                      << data.reader->context_ << ") for "
                      << "host = " << data.reader->context_->host() << " and "
                      << "path = " << data.reader->context_->method()
                      << " and request =\n"
                      << request.DebugString();

                  k.Emit(std::move(request));
                } else {
                  k.Fail(std::runtime_error("Failed to deserialize request"));
                }
              } else {
                EVENTUALS_GRPC_LOG(1)
                    << "Received notice of last request (or error) for call ("
                    << data.reader->context_ << ") for "
                    << "host = " << data.reader->context_->host() << " and "
                    << "path = " << data.reader->context_->method();

                // Signify end of stream (or error).
                k.Ended();
              }
            };
          }

          context_->stream()->Read(&data.buffer, &callback);
        });
  }

 private:
  template <typename T>
  static bool deserialize(::grpc::ByteBuffer* buffer, T* t) {
    auto status = ::grpc::SerializationTraits<T>::Deserialize(
        buffer,
        t);

    if (status.ok()) {
      return true;
    } else {
      EVENTUALS_GRPC_LOG(1)
          << "Failed to deserialize " << t->GetTypeName()
          << ": " << status.error_message() << std::endl;
      return false;
    }
  }

  // TODO(benh): explicitly borrow these for better safety (they come
  // from 'ServerCall' and outlive this 'ServerReader').
  ServerContext* context_;
};

////////////////////////////////////////////////////////////////////////

// 'ServerWriter' abstraction acts like the synchronous
// '::grpc::ServerWriter' but instead of the blocking 'Write*()'
// family of functions our functions all return an eventual!
template <typename ResponseType_>
class ServerWriter {
 public:
  // TODO(benh): borrow 'context' or the enclosing 'ServerCall' so
  // that we ensure that it doesn't get destructed while our
  // eventuals are still outstanding.
  ServerWriter(ServerContext* context)
    : context_(context) {}

  auto Write(
      ResponseType_ response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions()) {
    return Eventual<void>(
        [this,
         callback = Callback<bool>(),
         response = std::move(response),
         options = std::move(options)](auto& k) mutable {
          ::grpc::ByteBuffer buffer;
          if (serialize(response, &buffer)) {
            callback = [&k](bool ok) mutable {
              if (ok) {
                k.Start();
              } else {
                k.Fail(std::runtime_error("Failed to write"));
              }
            };

            EVENTUALS_GRPC_LOG(1)
                << "Sending response for call (" << context_ << ") for "
                << "host = " << context_->host() << " and "
                << "path = " << context_->method() << " and response =\n"
                << response.DebugString();

            context_->stream()->Write(buffer, options, &callback);
          } else {
            k.Fail(std::runtime_error("Failed to serialize response"));
          }
        });
  }

  auto WriteLast(
      ResponseType_ response,
      ::grpc::WriteOptions options = ::grpc::WriteOptions()) {
    return Eventual<void>(
        [this,
         callback = Callback<bool>(),
         response = std::move(response),
         options = std::move(options)](auto& k) mutable {
          ::grpc::ByteBuffer buffer;
          if (serialize(response, &buffer)) {
            EVENTUALS_GRPC_LOG(1)
                << "Sending last response for call (" << context_ << ") for "
                << "host = " << context_->host() << " and "
                << "path = " << context_->method() << " and response =\n"
                << response.DebugString();

            // NOTE: 'WriteLast()' will block until calling
            // 'Finish()' so we start the next continuation and
            // expect any errors to come from 'Finish()'.
            callback = [](bool ok) mutable {};
            context_->stream()->WriteLast(buffer, options, &callback);
            k.Start();
          } else {
            k.Fail(std::runtime_error("Failed to serialize response"));
          }
        });
  }

 private:
  template <typename T>
  static bool serialize(const T& t, ::grpc::ByteBuffer* buffer) {
    bool own = true;

    auto status = ::grpc::SerializationTraits<T>::Serialize(
        t,
        buffer,
        &own);

    if (status.ok()) {
      return true;
    } else {
      EVENTUALS_GRPC_LOG(1)
          << "Failed to serialize " << t.GetTypeName()
          << ": " << status.error_message() << std::endl;
      return false;
    }
  }

  // TODO(benh): explicitly borrow these for better safety (they come
  // from 'ServerCall' and outlive this 'ServerWriter').
  ServerContext* context_;
};

////////////////////////////////////////////////////////////////////////

// 'ServerCall' provides reading, writing, and finishing
// functionality for a gRPC call.
//
// NOTE: the semantics of the gRPC asynchronous APIs that we wrap must
// be respected. For example, you can't do more than one read at a
// time or more than one write at a time. This is relatively
// straightforward for read because it returns a stream, but there
// still isn't anything stopping you from calling 'Reader().Read()'
// multiple places. We don't do anything to check that you don't do
// that because gRPC doesn't check that either. It might not be that
// hard to check, but we've left that for a future project.
template <typename Request_, typename Response_>
class ServerCall {
 public:
  // We use 'RequestResponseTraits' in order to get the actual
  // request/response types vs the 'Stream<Request>' or
  // 'Stream<Response>' that you're currently required to specify so
  // that we can check at runtime that you've correctly specified the
  // method signature.
  using Traits_ = RequestResponseTraits;

  using RequestType_ = typename Traits_::Details<Request_>::Type;
  using ResponseType_ = typename Traits_::Details<Response_>::Type;

  ServerCall(std::unique_ptr<ServerContext>&& context)
    : context_(std::move(context)),
      reader_(context_.get()),
      writer_(context_.get()) {}

  ServerCall(ServerCall&& that)
    : context_(std::move(that.context_)),
      reader_(context_.get()),
      writer_(context_.get()) {}

  auto* context() {
    return context_->context();
  }

  auto& Reader() {
    return reader_;
  }

  auto& Writer() {
    return writer_;
  }

  auto Finish(const ::grpc::Status& status) {
    return Eventual<void>(
        [this,
         callback = Callback<bool>(),
         status](auto& k, auto&&...) mutable {
          callback = [&k](bool ok) {
            if (ok) {
              k.Start();
            } else {
              k.Fail(std::runtime_error("failed to finish"));
            }
          };

          EVENTUALS_GRPC_LOG(1)
              << "Finishing call (" << context_.get() << ") for "
              << "host = " << context_->host() << " and "
              << "path = " << context_->method();

          // TODO(benh): why aren't we calling 'FinishThenOnDone()'
          // defind in _our_ 'ServerContext' in order to overcome the
          // deficincies discussed there?
          context_->stream()->Finish(status, &callback);
        });
  }

  auto WaitForDone() {
    return Eventual<bool>(
        [this](auto& k, auto&&...) mutable {
          EVENTUALS_GRPC_LOG(1)
              << "Waiting for done on call (" << context_.get() << ") for "
              << "host = " << context_->host() << " and "
              << "path = " << context_->method();

          context_->OnDone([&k](bool cancelled) {
            k.Start(cancelled);
          });
        });
  }

 private:
  std::unique_ptr<ServerContext> context_;
  ServerReader<RequestType_> reader_;
  ServerWriter<ResponseType_> writer_;
};

////////////////////////////////////////////////////////////////////////

class Endpoint : public Synchronizable {
 public:
  Endpoint(std::string&& path, std::string&& host)
    : path_(std::move(path)),
      host_(std::move(host)) {}

  auto Enqueue(std::unique_ptr<ServerContext>&& context) {
    EVENTUALS_GRPC_LOG(1)
        << "Accepted call (" << context.get() << ") to "
        << "host = " << host_ << " for "
        << "path = " << path_;

    return pipe_.Write(std::move(context));
  }

  // NOTE: returns a stream rather than a single eventual context.
  auto Dequeue() {
    return pipe_.Read();
  }

  auto Shutdown() {
    return pipe_.Close();
  }

  const std::string& path() {
    return path_;
  }

  const std::string& host() {
    return host_;
  }

 private:
  const std::string path_;
  const std::string host_;

  Pipe<std::unique_ptr<ServerContext>> pipe_;
};

////////////////////////////////////////////////////////////////////////

class ServerStatus {
 public:
  static ServerStatus Ok() {
    return ServerStatus();
  }

  static ServerStatus Error(const std::string& error) {
    return ServerStatus(error);
  }

  bool ok() const {
    return !error_;
  }

  const std::string& error() const {
    return error_.value();
  }

 private:
  ServerStatus() {}
  ServerStatus(const std::string& error)
    : error_(error) {}

  std::optional<std::string> error_;
};

////////////////////////////////////////////////////////////////////////

class Server : public Synchronizable {
 public:
  ~Server();

  void Shutdown();

  void Wait();

  template <typename Service, typename Request, typename Response>
  auto Accept(std::string name, std::string host = "*");

  template <typename Request, typename Response>
  auto Accept(std::string name, std::string host = "*");

 private:
  friend class ServerBuilder;

  Server(
      std::vector<Service*>&& services,
      std::unique_ptr<::grpc::AsyncGenericService>&& service,
      std::unique_ptr<::grpc::Server>&& server,
      std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>>&& cqs,
      std::vector<std::thread>&& threads);

  template <typename Request, typename Response>
  auto Validate(const std::string& name);

  auto Insert(std::unique_ptr<Endpoint>&& endpoint);

  auto ShutdownEndpoints();

  auto RequestCall(ServerContext* context, ::grpc::ServerCompletionQueue* cq);

  auto Lookup(ServerContext* context);

  auto Unimplemented(ServerContext* context);

  std::unique_ptr<::grpc::AsyncGenericService> service_;
  std::unique_ptr<::grpc::Server> server_;
  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs_;
  std::vector<std::thread> threads_;

  struct Serve {
    Service* service;
    Interrupt interrupt;
    std::optional<Task::Of<void>> task;
    std::atomic<bool> done = false;
  };

  std::vector<std::unique_ptr<Serve>> serves_;

  struct Worker {
    Interrupt interrupt;
    std::optional<Task::Of<void>::With<::grpc::ServerCompletionQueue*>> task;
    std::atomic<bool> done = false;
  };

  std::vector<std::unique_ptr<Worker>> workers_;

  absl::flat_hash_map<
      std::pair<std::string, std::string>,
      std::unique_ptr<Endpoint>>
      endpoints_;
};

////////////////////////////////////////////////////////////////////////

struct ServerStatusOrServer {
  ServerStatus status;
  std::unique_ptr<Server> server;
};

////////////////////////////////////////////////////////////////////////

class ServerBuilder {
 public:
  ServerBuilder& SetNumberOfCompletionQueues(size_t n);

  // TODO(benh): Provide a 'setMaximumThreadsPerCompletionQueue' as well.
  ServerBuilder& SetMinimumThreadsPerCompletionQueue(size_t n);

  ServerBuilder& AddListeningPort(
      const std::string& address,
      std::shared_ptr<::grpc::ServerCredentials> credentials,
      int* selectedPort = nullptr);

  ServerBuilder& RegisterService(Service* service);

  ServerStatusOrServer BuildAndStart();

 private:
  ServerStatus status_ = ServerStatus::Ok();
  std::optional<size_t> numberOfCompletionQueues_;
  std::optional<size_t> minimumThreadsPerCompletionQueue_;
  std::vector<std::string> addresses_;
  std::vector<Service*> services_;

  ::grpc::ServerBuilder builder_;
};

////////////////////////////////////////////////////////////////////////

template <typename Request, typename Response>
auto Server::Validate(const std::string& name) {
  const auto* method =
      google::protobuf::DescriptorPool::generated_pool()
          ->FindMethodByName(name);

  return Eventual<void>([method](auto& k) {
    if (method == nullptr) {
      k.Fail(std::runtime_error("Method not found"));
    } else {
      using Traits = RequestResponseTraits;
      auto error = Traits::Validate<Request, Response>(method);
      if (error) {
        k.Fail(std::runtime_error(error->message));
      } else {
        k.Start();
      }
    }
  });
}

////////////////////////////////////////////////////////////////////////

inline auto Server::Insert(std::unique_ptr<Endpoint>&& endpoint) {
  return Synchronized(Eventual<void>(
      [this, endpoint = std::move(endpoint)](auto& k) mutable {
        auto key = std::make_pair(endpoint->path(), endpoint->host());

        auto [_, inserted] = endpoints_.try_emplace(key, std::move(endpoint));

        if (!inserted) {
          k.Fail(std::runtime_error(
              "Already serving " + endpoint->path()
              + " for host " + endpoint->host()));
        } else {
          EVENTUALS_GRPC_LOG(1)
              << "Serving endpoint for "
              << "host = " << key.second << " and "
              << "path = " << key.first;

          k.Start();
        }
      }));
}

////////////////////////////////////////////////////////////////////////

inline auto Server::ShutdownEndpoints() {
  return Synchronized(Then([this]() {
    return Iterate(endpoints_)
        | Map([](auto& entry) {
             auto& [_, endpoint] = entry;
             return endpoint->Shutdown();
           })
        | Loop();
  }));
}

////////////////////////////////////////////////////////////////////////

template <typename Service, typename Request, typename Response>
auto Server::Accept(std::string name, std::string host) {
  static_assert(
      IsService<Service>::value,
      "expecting \"Service\" type to be a protobuf 'Service'");

  return Accept<Request, Response>(
      std::string(Service::service_full_name()) + "." + name,
      std::move(host));
}

////////////////////////////////////////////////////////////////////////

template <typename Request, typename Response>
auto Server::Accept(std::string name, std::string host) {
  static_assert(
      IsMessage<Request>::value,
      "expecting \"request\" type to be a protobuf 'Message'");

  static_assert(
      IsMessage<Response>::value,
      "expecting \"response\" type to be a protobuf 'Message'");

  std::string path = "/" + name;
  size_t index = path.find_last_of(".");
  path.replace(index, 1, "/");

  auto endpoint = std::make_unique<Endpoint>(std::move(path), std::move(host));

  // NOTE: we need a generic/untyped "server context" object to be
  // able to store generic/untyped "endpoints" but we want to expose
  // the types below so that the compiler can enforce we use the right
  // request/response types on the 'ServerCall'.
  //
  // We only grab a pointer to 'endpoint' so we can move it via
  // 'Insert()' and we know that this code won't get executed if
  // 'Insert()' fails so we won't be using a dangling pointer.
  auto Dequeue = [endpoint = endpoint.get()]() {
    return endpoint->Dequeue()
        | Map([](auto&& context) {
             return ServerCall<Request, Response>(std::move(context));
           });
  };

  return Validate<Request, Response>(name)
      | Insert(std::move(endpoint))
      | Dequeue();
}

////////////////////////////////////////////////////////////////////////

// Helper that reads only a single request for a unary call
template <typename Request, typename Response>
auto UnaryPrologue(ServerCall<Request, Response>& call) {
  return call.Reader().Read()
      | Head(); // Only get the first request.
}

////////////////////////////////////////////////////////////////////////

// Helper that does the writing and finishing for a unary call as well
// as catching failures and handling appropriately.
template <typename Request, typename Response>
auto UnaryEpilogue(ServerCall<Request, Response>& call) {
  return Then([&](auto&& response) {
           return call.Writer().WriteLast(
               std::forward<decltype(response)>(response));
         })
      | Just(::grpc::Status::OK)
      | Catch([&](auto&&...) {
           return Just(
               ::grpc::Status(::grpc::UNKNOWN, "error"));
         })
      | Then([&](auto&& status) {
           return call.Finish(status)
               | call.WaitForDone();
         });
}

////////////////////////////////////////////////////////////////////////

// Helper that does the writing and finishing for a server streaming
// call as well as catching failures and handling appropriately.
template <typename Request, typename Response>
auto StreamingEpilogue(ServerCall<Request, Response>& call) {
  return Map([&](auto&& response) {
           return call.Writer().Write(response);
         })
      | Loop()
      | Just(::grpc::Status::OK)
      | Catch([&](auto&&...) {
           return Just(
               ::grpc::Status(::grpc::UNKNOWN, "error"));
         })
      | Then([&](auto&& status) {
           return call.Finish(status)
               | call.WaitForDone();
         });
}

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
