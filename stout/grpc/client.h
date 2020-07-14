#pragma once

#include <thread>

#include "absl/base/call_once.h"

#include "absl/synchronization/mutex.h"

#include "google/protobuf/descriptor.h"

#include "grpcpp/create_channel.h"
#include "grpcpp/completion_queue.h"

#include "grpcpp/generic/generic_stub.h"

#include "stout/borrowed_ptr.h"
#include "stout/notification.h"

#include "stout/grpc/call-base.h"
#include "stout/grpc/call-type.h"
#include "stout/grpc/traits.h"

namespace stout {
namespace grpc {

// Forward declarations.
class Client;

template <typename Request, typename Response>
class ClientCall;


enum class ClientCallStatus {
  // Used for internal state as well as for return status.
  Ok,
  WaitingForFinish,
  Finishing,
  Finished,

  // Used only for return status.
  WritingUnavailable, // Call most likely dead.
  OnReadCalledMoreThanOnce,
  OnFinishedCalledMoreThanOnce,
  FailedToSerializeRequest,
};


inline std::string stringify(ClientCallStatus status)
{
  switch (status) {
    case ClientCallStatus::Ok:
      return "Ok";
    case ClientCallStatus::WaitingForFinish:
      return "WaitingForFinish";
    case ClientCallStatus::Finishing:
      return "Finishing";
    case ClientCallStatus::Finished:
      return "Finished";
    case ClientCallStatus::WritingUnavailable:
      return "WritingUnavailable";
    case ClientCallStatus::OnReadCalledMoreThanOnce:
      return "OnReadCalledMoreThanOnce";
    case ClientCallStatus::OnFinishedCalledMoreThanOnce:
      return "OnFinishedCalledMoreThanOnce";
    case ClientCallStatus::FailedToSerializeRequest:
      return "FailedToSerializeRequest";
  }
}


class ClientCallBase : public CallBase
{
public:
  ClientCallBase(CallType type);

  ::grpc::ClientContext* context()
  {
    return &context_;
  }

  ClientCallStatus Finish();

protected:
  template <typename Response, typename F>
  ClientCallStatus OnRead(F&& f)
  {
    mutex_.Lock();

    auto status = status_;

    mutex_.Unlock();

    // NOTE: caller might have done a 'WritesDone()' or a
    // 'WriteAndDone()' before setting up the read handler, hence we
    // allow 'WaitingForFinish' in addition to 'Ok'.
    if (status == ClientCallStatus::Ok
        || status == ClientCallStatus::WaitingForFinish) {

      status = ClientCallStatus::OnReadCalledMoreThanOnce;

      // NOTE: we set callback here vs constructor so we can capture 'f'.
      absl::call_once(read_once_, [this, &f, &status]() {
        read_callback_ = [this, f = std::forward<F>(f)](
            bool ok, void*) mutable {
          if (ok) {
            // TODO(benh): Provide an allocator for responses.
            std::unique_ptr<Response> response(new Response());
              if (deserialize(&read_buffer_, response.get())) {
                f(std::move(response));
              }
              // Keep reading if the server is streaming.
              if (type_ == CallType::SERVER_STREAMING
                  || type_ == CallType::BIDI_STREAMING) {
                stream_->Read(&read_buffer_, &read_callback_);
              }
          } else {
            // Signify end of stream (or error).
            //
            // TODO(benh): Consider a separate callback for errors? The
            // value of making this part of 'OnRead' is that it forces
            // users to handle errors, versus never calling 'OnError'
            // and therefore missing errors.
            f(std::unique_ptr<Response>(nullptr));
          }
        };

        stream_->Read(&read_buffer_, &read_callback_);

        status = ClientCallStatus::Ok;
        });
    }

    return status;
  }

  ClientCallStatus WritesDone();

  ClientCallStatus WritesDoneAndFinish()
  {
    auto status = WritesDone();

    if (status != ClientCallStatus::Ok) {
      return status;
    }

    return Finish();
  }

  template <typename F>
  ClientCallStatus WritesDoneAndFinish(F&& f)
  {
    auto status = WritesDone();

    if (status != ClientCallStatus::Ok) {
      return status;
    }

    return Finish(std::forward<F>(f));
  }

  template <typename Request, typename Callback>
  ClientCallStatus Write(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    ::grpc::ByteBuffer buffer;

    if (!serialize(request, &buffer)) {
      return ClientCallStatus::FailedToSerializeRequest;
    }

    absl::MutexLock lock(&mutex_);

    if (status_ != ClientCallStatus::Ok) {
      return status_;
    }

    return write(&buffer, options, std::forward<Callback>(callback));
  }

  template <typename Request, typename Callback>
  ClientCallStatus WriteAndDone(
      const Request& request,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
  {
    auto status = Write(request, options, std::forward<Callback>(callback));

    if (status != ClientCallStatus::Ok) {
      return status;
    }

    return ClientCallBase::WritesDone();
  }

  template <typename F>
  ClientCallStatus OnFinished(F&& f)
  {
    auto status = ClientCallStatus::OnFinishedCalledMoreThanOnce;

    // NOTE: we set callback here vs constructor so we can capture 'f'.
    absl::call_once(finish_once_, [this, &f, &status]() {
      finish_callback_ = [this, f = std::forward<F>(f)](bool ok, void*) mutable {
        mutex_.Lock();
        status_ = ClientCallStatus::Finished;
        mutex_.Unlock();

        if (ok) {
          f(finish_status_.value());
        }

        finished_.Notify(ok);
      };

      status = ClientCallStatus::Ok;
    });

    return status;
  }

  template <typename F>
  ClientCallStatus Finish(F&& f)
  {
    auto status = OnFinished(std::forward<F>(f));

    if (status != ClientCallStatus::Ok) {
      return status;
    }

    return Finish();
  }

private:
  friend class Client;

  template <typename F>
  void Start(
      std::unique_ptr<::grpc::GenericClientAsyncReaderWriter> stream,
      F&& f)
  {
    stream_ = std::move(stream);

    // NOTE: we set callback here vs constructor so we can capture 'f'.
    start_callback_ = [this, f = std::forward<F>(f)](bool ok, void*) {
      f(ok, &finished_);
    };

    stream_->StartCall(&start_callback_);
  }

  template <typename Callback>
  ClientCallStatus write(
      ::grpc::ByteBuffer* buffer,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
    EXCLUSIVE_LOCKS_REQUIRED(mutex_)
  {
    if (!write_callback_) {
      return ClientCallStatus::WritingUnavailable;
    }

    write_datas_.emplace_back();

    auto& data = write_datas_.back();

    data.buffer.Swap(buffer);
    data.options = options;
    data.callback = std::forward<Callback>(callback);

    if (!writing_) {
      buffer = &write_datas_.front().buffer;
      writing_ = true;
    } else {
      buffer = nullptr;
    }

    mutex_.Unlock();

    if (buffer != nullptr) {
      stream_->Write(*buffer, options, &write_callback_);
    }

    mutex_.Lock();

    return ClientCallStatus::Ok;
  }

  absl::Mutex mutex_;

  ClientCallStatus status_ = ClientCallStatus::Ok;

  ::grpc::ClientContext context_;
  
  borrowed_ptr<::grpc::GenericClientAsyncReaderWriter> stream_;

  std::function<void(bool, void*)> start_callback_;

  absl::once_flag read_once_;
  std::function<void(bool, void*)> read_callback_;
  ::grpc::ByteBuffer read_buffer_;

  bool writing_ = false;
  bool writes_done_ = false;
  std::function<void(bool, void*)> write_callback_;

  struct WriteData
  {
    ::grpc::ByteBuffer buffer;
    ::grpc::WriteOptions options;
    std::function<void(bool)> callback;
  };

  std::list<WriteData> write_datas_;

  absl::once_flag finish_once_;
  std::function<void(bool, void*)> finish_callback_;
  absl::optional<::grpc::Status> finish_status_ = absl::nullopt;
  Notification<bool> finished_;

  CallType type_;
};


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


class ClientStatus
{
public:
  static ClientStatus Ok()
  {
    return ClientStatus();
  }

  static ClientStatus Error(const std::string& error)
  {
    return ClientStatus(error);
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
  ClientStatus() {}
  ClientStatus(const std::string& error) : error_(error) {}

  absl::optional<std::string> error_;
};


class Client
{
public:
  Client(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials)
    : channel_(::grpc::CreateChannel(target, credentials)), stub_(channel_)
  {
    // TODO(benh): Support more than one thread, e.g., there could be
    // a thread reading while there is a thread writing, or even
    // multiple threads processing responses concurrently.
    thread_ = std::thread(
        [cq = &cq_]() {
          void* tag = nullptr;
          bool ok = false;
          while (cq->Next(&tag, &ok)) {
            (*static_cast<std::function<void(bool, void*)>*>(tag))(ok, tag);
          }
        });
  }

  ~Client()
  {
    Shutdown();
    Wait();
  }

  void Shutdown()
  {
    // Client might have been moved, use 'thread_' to distinguish.
    if (thread_.joinable()) {
      cq_.Shutdown();
    }
  }

  void Wait()
  {
    // Client might have been moved, use 'thread_' to distinguish.
    if (thread_.joinable()) {
      thread_.join();

      void* tag = nullptr;
      bool ok = false;
      while (cq_.Next(&tag, &ok)) {}
    }
  }

  template <
    typename Service,
    typename Request,
    typename Response,
    typename Read,
    typename Finished>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ClientCall<Request, Response>, Response>::value
    && IsFinishedHandler<Finished, ClientCall<Request, Response>>::value,
    ClientStatus> Call(
        const std::string& name,
        const std::string& host,
        const typename RequestResponseTraits::Details<Request>::Type* request,
        Read&& read,
        Finished&& finished)
  {
    return Call<Request, Response, Read, Finished>(
        std::string(Service::service_full_name()) + "." + name,
        request,
        host,
        std::forward<Read>(read),
        std::forward<Finished>(finished));
  }

  template <
    typename Service,
    typename Request,
    typename Response,
    typename Read,
    typename Finished>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ClientCall<Request, Response>, Response>::value
    && IsFinishedHandler<Finished, ClientCall<Request, Response>>::value,
    ClientStatus> Call(
        const std::string& name,
        const typename RequestResponseTraits::Details<Request>::Type* request,
        Read&& read,
        Finished&& finished)
  {
    return Call<Request, Response, Read, Finished>(
        std::string(Service::service_full_name()) + "." + name,
        absl::nullopt,
        request,
        std::forward<Read>(read),
        std::forward<Finished>(finished));
  }

  template <
    typename Request,
    typename Response,
    typename Read,
    typename Finished>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ClientCall<Request, Response>, Response>::value
    && IsFinishedHandler<Finished, ClientCall<Request, Response>>::value,
    ClientStatus> Call(
        const std::string& name,
        const typename RequestResponseTraits::Details<Request>::Type* request,
        Read&& read,
        Finished&& finished)
  {
    return Call<Request, Response, Read, Finished>(
        name,
        absl::nullopt,
        request,
        std::forward<Read>(read),
        std::forward<Finished>(finished));
  }

  template <
    typename Request,
    typename Response,
    typename Read,
    typename Finished>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsReadHandler<Read, ClientCall<Request, Response>, Response>::value
    && IsFinishedHandler<Finished, ClientCall<Request, Response>>::value,
    ClientStatus> Call(
        const std::string& name,
        const absl::optional<std::string>& host,
        const typename RequestResponseTraits::Details<Request>::Type* request,
        Read&& read,
        Finished&& finished)
  {
    return Call<Request, Response>(
        name,
        host,
        [request,
         read = std::forward<Read>(read),
         finished = std::forward<Finished>(finished)](auto&& call, bool ok) {
          absl::optional<::grpc::Status> error = absl::nullopt;
          if (!ok) {
            error = ::grpc::Status(
                ::grpc::UNAVAILABLE,
                "channel is either permanently broken or transiently broken"
                " but with the fail-fast option");
          } else {
            auto status = [&]() {
              switch (RequestResponseTraits::Type<Request, Response>()) {
                case CallType::UNARY:
                case CallType::SERVER_STREAMING:
                  return call->WriteAndDone(*request);
                default:
                  // NOTE: because 'Client' is a friend of
                  // 'ClientCallBase' we won't get a compile time
                  // error even if the type of 'call' doesn't allow us
                  // to call 'Write()'.
                  return call->Write(
                      *request,
                      ::grpc::WriteOptions(),
                      std::function<void(bool)>());
              }
            }();

            switch (status) {
              case ClientCallStatus::Ok:
                break;
              case ClientCallStatus::FailedToSerializeRequest:
                error = ::grpc::Status(
                    ::grpc::INVALID_ARGUMENT,
                    "failed to serialize request");
                break;
              default:
                error = ::grpc::Status(
                    ::grpc::INTERNAL,
                    "ClientCallStatus is " + stringify(status));
                break;
                
            }
          }

          call->OnFinished([error, finished = std::move(finished)](
              auto* call, const ::grpc::Status& status) {
            if (!error) {
              finished(call, status);
            } else {
              // NOTE: we're expecting 'status' to indicate a
              // cancelled call because we invoked 'TryCancel()' but
              // really this is due to an error encountered above
              // (e.g., 'StartCall()' yielded '!ok').

              finished(call, error.value());
            }
          });

          if (error) {
            call->context()->TryCancel();
            call->Finish();
          } else {
            call->OnRead(std::move(read));
          }
        });
  }

  template <typename Service, typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ClientCall<Request, Response>, bool>::value,
    ClientStatus> Call(
        const std::string& name,
        const std::string& host,
        Handler&& handler)
  {
    return Call<Request, Response, Handler>(
        std::string(Service::service_full_name()) + "." + name,
        host,
        std::forward<Handler>(handler));
  }

  template <typename Service, typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsService<Service>::value
    && IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ClientCall<Request, Response>, bool>::value,
    ClientStatus> Call(
        const std::string& name,
        Handler&& handler)
  {
    return Call<Request, Response, Handler>(
        std::string(Service::service_full_name()) + "." + name,
        absl::nullopt,
        std::forward<Handler>(handler));
  }

  template <typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ClientCall<Request, Response>, bool>::value,
    ClientStatus> Call(
        const std::string& name,
        Handler&& handler)
  {
    return Call<Request, Response, Handler>(
        name,
        absl::nullopt,
        std::forward<Handler>(handler));
  }

  template <typename Request, typename Response, typename Handler>
  std::enable_if_t<
    IsMessage<Request>::value
    && IsMessage<Response>::value
    && IsCallHandler<Handler, ClientCall<Request, Response>, bool>::value,
    ClientStatus> Call(
        const std::string& name,
        const absl::optional<std::string>& host,
        Handler&& handler)
  {
    const auto* method = google::protobuf::DescriptorPool::generated_pool()
      ->FindMethodByName(name);

    if (method == nullptr) {
      return ClientStatus::Error("Method not found");
    }

    auto error = RequestResponseTraits::Validate<Request, Response>(method);

    if (error) {
      return ClientStatus::Error(error->message);
    }

    std::string path = "/" + name;

    size_t index = path.find_last_of(".");

    path.replace(index, 1, "/");

    // TODO(benh): Check status of channel, is this a redundant check
    // because PrepareCall also does this? At the very least we'll
    // probably give a better error message by checking.

    // TODO(benh): Provide an allocator for calls.
    auto* call = new ClientCall<Request, Response>();

    auto* context = call->context();

    if (host) {
      context->set_authority(host.value());
    }

    auto stream = stub_.PrepareCall(context, path, &cq_);

    if (!stream) {
      delete call;
      return ClientStatus::Error("GenericStub::PrepareCall failed");
    }

    call->Start(
        std::move(stream),
        [call, handler = std::forward<Handler>(handler)](
            bool ok, Notification<bool>* finished) {
          handler(borrow(call, [finished](auto* call) {
            finished->Watch([call](bool) {
              delete call;
            });
          }),
          ok);
        });

    return ClientStatus::Ok();
  }

private:
  std::shared_ptr<::grpc::Channel> channel_;
  ::grpc::GenericStub stub_;
  ::grpc::CompletionQueue cq_;
  std::thread thread_;
};

} // namespace grpc {
} // namespace stout {
