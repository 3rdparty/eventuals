#pragma once

#include <cassert>
#include <deque>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/descriptor.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/generic/async_generic_service.h"
#include "grpcpp/impl/codegen/proto_utils.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "stout/borrowable.h"
#include "stout/eventual.h"
#include "stout/grpc/logging.h"
#include "stout/grpc/traits.h"
#include "stout/lambda.h"
#include "stout/lock.h"
#include "stout/notification.h"
#include "stout/repeat.h"
#include "stout/task.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

using stout::eventuals::detail::operator|;

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

  Notification<bool> done_;
};

////////////////////////////////////////////////////////////////////////

template <typename Request_, typename Response_>
struct TypedServerContext {
  std::unique_ptr<ServerContext> context;
};

////////////////////////////////////////////////////////////////////////

class Endpoint : public Synchronizable {
 public:
  Endpoint(std::string&& path, std::string&& host)
    : path_(std::move(path)),
      host_(std::move(host)) {}

  auto Enqueue(std::unique_ptr<ServerContext>&& context) {
    return Synchronized(Lambda([this, context = std::move(context)]() mutable {
      contexts_.emplace_back(std::move(context));
      notify_();
    }));
  }

  auto Dequeue() {
    return Synchronized(
        Wait([this](auto notify) {
          notify_ = std::move(notify);
          return [this]() {
            return contexts_.empty();
          };
        })
        | Lambda([this]() {
            CHECK(!contexts_.empty());
            auto context = std::move(contexts_.front());
            contexts_.pop_front();
            return context;
          }));
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

  // NOTE: callback is initially a no-op so 'Enqueue()' can be called
  // before the callback has been set up in 'Dequeue()'.
  Callback<> notify_ = []() {};

  std::deque<std::unique_ptr<ServerContext>> contexts_;
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

  template <typename Value, typename Request, typename Response>
  static auto Handler(TypedServerContext<Request, Response>&& context);

  template <typename Request, typename Response>
  static auto Handler(TypedServerContext<Request, Response>&& context);

  template <typename Service, typename Request, typename Response>
  auto Accept(std::string name, std::string host = "*");

  template <typename Request, typename Response>
  auto Accept(std::string name, std::string host = "*");

 private:
  friend class ServerBuilder;

  Server(
      std::unique_ptr<::grpc::AsyncGenericService>&& service,
      std::unique_ptr<::grpc::Server>&& server,
      std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>>&& cqs,
      std::vector<std::thread>&& threads);

  template <typename Request, typename Response>
  auto Validate(const std::string& name);

  auto Insert(std::unique_ptr<Endpoint>&& endpoint);

  auto RequestCall(ServerContext* context, ::grpc::ServerCompletionQueue* cq);

  auto Lookup(ServerContext* context);

  auto Unimplemented(ServerContext* context);

  std::unique_ptr<::grpc::AsyncGenericService> service_;
  std::unique_ptr<::grpc::Server> server_;
  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs_;
  std::vector<std::thread> threads_;

  struct Worker {
    Interrupt interrupt;
    std::optional<Task<Undefined>::With<::grpc::ServerCompletionQueue*>> task;
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

  ServerStatusOrServer BuildAndStart();

 private:
  ServerStatus status_ = ServerStatus::Ok();
  std::optional<size_t> numberOfCompletionQueues_;
  std::optional<size_t> minimumThreadsPerCompletionQueue_;
  std::vector<std::string> addresses_;

  ::grpc::ServerBuilder builder_;

  std::unique_ptr<::grpc::AsyncGenericService> service_;
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

        if (!endpoints_.try_emplace(key, std::move(endpoint)).second) {
          k.Fail(std::runtime_error(
              "Already serving " + endpoint->path()
              + " for host " + endpoint->host()));
        } else {
          k.Start();
        }
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
  // request/response types so we use the below helper to reintroduce
  // the types using a 'TypedServerContext'.
  //
  // We only grab a pointer to 'endpoint' so we can move it via
  // 'Insert()' and we know that this code won't get executed if
  // 'Insert()' fails so we won't be using a dangling pointer.
  auto Dequeue = [endpoint = endpoint.get()]() {
    return endpoint->Dequeue()
        | Lambda([](auto&& context) {
             return TypedServerContext<Request, Response>{std::move(context)};
           });
  };

  return Validate<Request, Response>(name)
      | Insert(std::move(endpoint))
      | Repeat(Dequeue());
}

////////////////////////////////////////////////////////////////////////

struct _ServerHandler {
  template <
      typename K_,
      typename Request_,
      typename Response_,
      typename Ready_,
      typename Body_,
      typename Finished_>
  struct Continuation {
    using Traits_ = RequestResponseTraits;

    using RequestType_ = typename Traits_::Details<Request_>::Type;
    using ResponseType_ = typename Traits_::Details<Response_>::Type;

    Continuation(
        K_ k,
        std::unique_ptr<ServerContext>&& context,
        Ready_ ready,
        Body_ body,
        Finished_ finished)
      : k_(std::move(k)),
        context_(std::move(context)),
        ready_(std::move(ready)),
        body_(std::move(body)),
        finished_(std::move(finished)) {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        context_(std::move(that.context_)),
        ready_(std::move(that.ready_)),
        body_(std::move(that.body_)),
        finished_(std::move(that.finished_)) {
      // NOTE: only expecting move construction before starting.
      CHECK(!read_callback_) << "moving after starting";
    }

    ~Continuation() {
      /// Wait for 'done' so that we know that grpc will let us free
      // it, but only wait for "done" if we actually started.
      if (read_callback_) {
        while (!done_.load()) {
          // TODO(benh): portably pause CPU as part of spin loop and
          // consider doing a LOG_EVERY(WARNING, N) as well.
        }
      }
    }

    void Start() {
      if (handler_) {
        if (!handler_->Install()) {
          handler_->Invoke();
        }
      }

      read_callback_ = [this](bool ok) mutable {
        if constexpr (IsUndefined<Body_>::value) {
          if (ok) {
            stream()->Read(&read_buffer_, &read_callback_);
          }
        } else {
          if (ok) {
            if (deserialize(&read_buffer_, request_.get())) {
              auto request = request_.Borrow();
              request_.Watch([this]() {
                stream()->Read(&read_buffer_, &read_callback_);
              });
              body_(*this, std::move(request));
            } else {
              LOG(WARNING) << "dropping request that failed to deserialize";
            }
          } else {
            // Signify end of stream (or error).
            body_(*this, borrowed_ptr<RequestType_>());
          }
        }
      };

      write_callback_ = [this](bool ok) {
        if (ok) {
          ::grpc::ByteBuffer* buffer = nullptr;
          ::grpc::WriteOptions* options = nullptr;
          ::grpc::Status* status = nullptr;

          {
            absl::MutexLock l(&mutex_);

            CHECK(!write_datas_.empty());
            write_datas_.pop_front();

            if (!write_datas_.empty()) {
              buffer = &write_datas_.front().buffer;
              options = &write_datas_.front().options;
            } else if (status_) {
              status = &status_.value();
            }
          }

          if (buffer != nullptr) {
            stream()->Write(*buffer, *options, &write_callback_);
          } else if (status != nullptr) {
            stream()->Finish(*status, &finish_callback_);
          }
        } else {
          absl::MutexLock l(&mutex_);
          write_datas_.clear();
          write_callback_ = Callback<bool>();
        }
      };

      finish_callback_ = [this](bool /* ok */) {
        // NOTE: it's possible that we may get a finish callback from
        // calling 'Finish()' (or 'WriteAndFinish()' as well as from
        // doing an 'AsyncNotifyWhenDone()' so we use
        // 'std::call_once()' to make sure we only "finish" once.
        std::call_once(finish_, [this]() {
          if constexpr (IsUndefined<Finished_>::value) {
            k_.Start(context()->IsCancelled());
          } else {
            finished_(*this, k_);
          }
        });
      };

      context_->OnDone([this](bool /* cancelled */) {
        finish_callback_(/* ok = */ true);
        done_.store(true);
      });

      if constexpr (!IsUndefined<Ready_>::value) {
        ready_(*this);
      }

      stream()->Read(&read_buffer_, &read_callback_);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // TODO(benh): TryCancel() so that we don't leak resources?
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      // TODO(benh): TryCancel() so that we don't leak resources?
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      handler_.emplace(&interrupt, [this]() {
        context()->TryCancel();
      });
    }

    void Write(
        const ResponseType_& response,
        const ::grpc::WriteOptions& options = ::grpc::WriteOptions()) {
      ::grpc::ByteBuffer buffer;

      if (serialize(response, &buffer)) {
        WriteMaybeFinish(&buffer, options, std::nullopt);
      } else {
        LOG(WARNING) << "dropping response that failed to serialize";
      }
    }

    void WriteLast(
        const ResponseType_& response,
        ::grpc::WriteOptions options = ::grpc::WriteOptions()) {
      options.set_last_message();
      Write(response, options);
    }

    void WriteAndFinish(
        const ResponseType_& response,
        const ::grpc::WriteOptions& options,
        const ::grpc::Status& status) {
      ::grpc::ByteBuffer buffer;

      if (serialize(response, &buffer)) {
        WriteMaybeFinish(&buffer, options, status);
      } else {
        LOG(WARNING) << "dropping response that failed to serialize";
      }
    }

    void WriteAndFinish(
        const ResponseType_& response,
        const ::grpc::Status& status) {
      WriteAndFinish(response, ::grpc::WriteOptions(), status);
    }

    void Finish(const ::grpc::Status& status) LOCKS_EXCLUDED(mutex_) {
      {
        absl::MutexLock l(&mutex_);
        status_ = status;
        if (!write_datas_.empty()) {
          return;
        }
      }
      stream()->Finish(status, &finish_callback_);
    }

    auto* context() {
      return context_->context();
    }

    auto* stream() {
      return context_->stream();
    }

    template <typename T>
    bool serialize(const T& t, ::grpc::ByteBuffer* buffer) {
      bool own = true;

      auto status = ::grpc::SerializationTraits<T>::Serialize(
          t,
          buffer,
          &own);

      if (status.ok()) {
        return true;
      } else {
        STOUT_GRPC_LOG(1)
            << "failed to serialize " << t.GetTypeName()
            << ": " << status.error_message() << std::endl;
        return false;
      }
    }

    template <typename T>
    bool deserialize(::grpc::ByteBuffer* buffer, T* t) {
      auto status = ::grpc::SerializationTraits<T>::Deserialize(
          buffer,
          t);

      if (status.ok()) {
        return true;
      } else {
        STOUT_GRPC_LOG(1)
            << "failed to deserialize " << t->GetTypeName()
            << ": " << status.error_message() << std::endl;
        return false;
      }
    }

    void WriteMaybeFinish(
        ::grpc::ByteBuffer* buffer,
        const ::grpc::WriteOptions& options,
        const std::optional<::grpc::Status>& status) LOCKS_EXCLUDED(mutex_) {
      absl::ReleasableMutexLock l(&mutex_);

      if (write_callback_) {
        write_datas_.emplace_back();

        auto& data = write_datas_.back();

        data.buffer.Swap(buffer);
        data.options = options;

        if (write_datas_.size() == 1) {
          buffer = &write_datas_.front().buffer;
        } else {
          buffer = nullptr;
        }

        l.Release();

        if (buffer != nullptr) {
          if (!status) {
            stream()->Write(*buffer, options, &write_callback_);
          } else {
            stream()->WriteAndFinish(
                *buffer,
                options,
                status.value(),
                &finish_callback_);
          }
        }
      }
    }

    K_ k_;
    std::unique_ptr<ServerContext> context_;
    Ready_ ready_;
    Body_ body_;
    Finished_ finished_;

    std::optional<Interrupt::Handler> handler_;

    Callback<bool> read_callback_;
    Callback<bool> write_callback_;
    Callback<bool> finish_callback_;

    ::grpc::ByteBuffer read_buffer_;
    Borrowable<RequestType_> request_;

    struct WriteData {
      ::grpc::ByteBuffer buffer;
      ::grpc::WriteOptions options;
    };

    // TODO(benh): render this lock-free.
    absl::Mutex mutex_;
    std::list<WriteData> write_datas_ GUARDED_BY(mutex_);
    std::optional<::grpc::Status> status_ GUARDED_BY(mutex_);

    std::once_flag finish_;
    std::atomic<bool> done_ = false;
  };

  template <
      typename Request_,
      typename Response_,
      typename Ready_,
      typename Body_,
      typename Finished_,
      typename Value_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Value_;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Request_, Response_, Ready_, Body_, Finished_>(
          std::move(k),
          std::move(context_),
          std::move(ready_),
          std::move(body_),
          std::move(finished_));
    }

    template <
        typename Request,
        typename Response,
        typename Value,
        typename Ready,
        typename Body,
        typename Finished>
    static auto create(
        std::unique_ptr<ServerContext>&& context,
        Ready ready,
        Body body,
        Finished finished) {
      return Composable<Request, Response, Ready, Body, Finished, Value>{
          std::move(context),
          std::move(ready),
          std::move(body),
          std::move(finished)};
    }

    template <typename Ready>
    auto ready(Ready ready) && {
      static_assert(IsUndefined<Ready_>::value, "Duplicate 'ready'");
      return create<Request_, Response_, Value_>(
          std::move(context_),
          std::move(ready),
          std::move(body_),
          std::move(finished_));
    }

    template <typename Body>
    auto body(Body body) && {
      static_assert(IsUndefined<Body_>::value, "Duplicate 'body'");
      return create<Request_, Response_, Value_>(
          std::move(context_),
          std::move(ready_),
          std::move(body),
          std::move(finished_));
    }

    template <typename Finished>
    auto finished(Finished finished) && {
      static_assert(IsUndefined<Finished_>::value, "Duplicate 'finished'");
      return create<Request_, Response_, Value_>(
          std::move(context_),
          std::move(ready_),
          std::move(body_),
          std::move(finished));
    }

    std::unique_ptr<ServerContext> context_;
    Ready_ ready_;
    Body_ body_;
    Finished_ finished_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename Value, typename Request, typename Response>
auto Server::Handler(TypedServerContext<Request, Response>&& context) {
  return _ServerHandler::Composable<
      Request,
      Response,
      Undefined,
      Undefined,
      Undefined,
      Value>{
      std::move(context.context)};
}

////////////////////////////////////////////////////////////////////////

template <typename Request, typename Response>
auto Server::Handler(TypedServerContext<Request, Response>&& context) {
  return _ServerHandler::Composable<
      Request,
      Response,
      Undefined,
      Undefined,
      Undefined,
      bool>{
      std::move(context.context)};
}

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
