#pragma once

#include "eventuals/callback.h"
#include "eventuals/eventual.h"
#include "eventuals/grpc/completion-pool.h"
#include "eventuals/grpc/logging.h"
#include "eventuals/grpc/traits.h"
#include "eventuals/lazy.h"
#include "eventuals/stream.h"
#include "eventuals/then.h"
#include "grpcpp/client_context.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/generic/generic_stub.h"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

// 'ClientReader' abstraction acts like the synchronous
// '::grpc::ClientReader' but instead of a blocking 'Read()' call we
// return a stream!
template <typename ResponseType_>
class ClientReader {
 public:
  // TODO(benh): borrow 'stream' or the enclosing 'ClientCall' so
  // that we ensure that it doesn't get destructed while our
  // eventuals are still outstanding.
  ClientReader(
      const std::string& path,
      const std::optional<std::string>& host,
      ::grpc::ClientContext* context,
      ::grpc::internal::AsyncReaderInterface<ResponseType_>* stream)
    : path_(path),
      host_(host),
      context_(context),
      stream_(stream) {}

  auto Read() {
    struct Data {
      ClientReader* reader = nullptr;
      ResponseType_ response;
      void* k = nullptr;
    };
    return eventuals::Stream<ResponseType_>()
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
                EVENTUALS_GRPC_LOG(1)
                    << "Received response for call ("
                    << data.reader->context_ << ")"
                    << " with host = " << data.reader->host_.value_or("*")
                    << " with path = " << data.reader->path_
                    << " and response =\n"
                    << data.response.DebugString();

                k.Emit(std::move(data.response));
              } else {
                EVENTUALS_GRPC_LOG(1)
                    << "Received notice of last response (or error) for call ("
                    << data.reader->context_ << ")"
                    << " with host = " << data.reader->host_.value_or("*")
                    << " with path = " << data.reader->path_;

                // Signify end of stream (or error).
                k.Ended();
              }
            };
          }

          stream_->Read(&data.response, &callback);
        });
  }

 private:
  // TODO(benh): explicitly borrow these for better safety (they come
  // from 'ClientCall' and outlive this 'ClientReader').
  const std::string& path_;
  const std::optional<std::string>& host_;
  ::grpc::ClientContext* context_;

  // TODO(benh): don't depend on 'internal' types ... doing so now so
  // users don't have to include 'RequestType_' as part of
  // 'ClientReader' when they write out the full type.
  ::grpc::internal::AsyncReaderInterface<ResponseType_>* stream_;
};

////////////////////////////////////////////////////////////////////////

// 'ClientWriter' abstraction acts like the synchronous
// '::grpc::ClientWriter' but instead of the blocking 'Write*()'
// family of functions our functions all return an eventual!
template <typename RequestType_>
class ClientWriter {
 public:
  // TODO(benh): borrow 'stream' or the enclosing 'ClientCall' so that
  // we ensure that it doesn't get destructed while our eventuals are
  // still outstanding.
  ClientWriter(
      const std::string& path,
      const std::optional<std::string>& host,
      ::grpc::ClientContext* context,
      ::grpc::internal::AsyncWriterInterface<RequestType_>* stream)
    : path_(path),
      host_(host),
      context_(context),
      stream_(stream) {}

  auto Write(
      RequestType_ request,
      ::grpc::WriteOptions options = ::grpc::WriteOptions()) {
    return Eventual<void>()
        .raises<std::runtime_error>()
        .start(
            [this,
             callback = Callback<bool>(),
             request = std::move(request),
             options = std::move(options)](auto& k) mutable {
              callback = [&k](bool ok) mutable {
                if (ok) {
                  k.Start();
                } else {
                  k.Fail(std::runtime_error("Failed to write"));
                }
              };

              EVENTUALS_GRPC_LOG(1)
                  << "Sending " << (options.is_last_message() ? "(last)" : "")
                  << " request for call (" << context_ << ")"
                  << " with host = " << host_.value_or("*")
                  << " with path = " << path_
                  << " and request =\n"
                  << request.DebugString();

              stream_->Write(request, options, &callback);
            });
  }

  auto WriteLast(
      RequestType_ request,
      ::grpc::WriteOptions options = ::grpc::WriteOptions()) {
    return Write(request, options.set_last_message());
  }

 private:
  // TODO(benh): explicitly borrow these for better safety (they come
  // from 'ClientCall' and outlive this 'ClientWriter').
  const std::string& path_;
  const std::optional<std::string>& host_;
  ::grpc::ClientContext* context_;

  // TODO(benh): don't depend on 'internal' types ... doing so now so
  // users don't have to include 'ResponseType_' as part of
  // 'ClientWriter' when they write out the full type.
  ::grpc::internal::AsyncWriterInterface<RequestType_>* stream_;
};

////////////////////////////////////////////////////////////////////////

template <typename Request_, typename Response_>
class ClientCall {
 public:
  // We use 'RequestResponseTraits' in order to get the actual
  // request/response types vs the 'Stream<Request>' or
  // 'Stream<Response>' that you're currently required to specify so
  // that we can check at runtime that you've correctly specified the
  // method signature.
  using Traits_ = RequestResponseTraits;

  using RequestType_ = typename Traits_::Details<Request_>::Type;
  using ResponseType_ = typename Traits_::Details<Response_>::Type;

  ClientCall(
      const std::string& path,
      const std::optional<std::string>& host,
      ::grpc::ClientContext* context,
      stout::borrowed_ptr<::grpc::CompletionQueue>&& cq,
      ::grpc::TemplatedGenericStub<RequestType_, ResponseType_>&& stub,
      std::unique_ptr<
          ::grpc::ClientAsyncReaderWriter<
              RequestType_,
              ResponseType_>>&& stream)
    : path_(path),
      host_(host),
      context_(context),
      cq_(std::move(cq)),
      stub_(std::move(stub)),
      stream_(std::move(stream)),
      reader_(path_, host_, context_, stream_.get()),
      writer_(path_, host_, context_, stream_.get()) {}

  auto* context() {
    return context_;
  }

  auto& Reader() {
    return reader_;
  }

  auto& Writer() {
    return writer_;
  }

  // TODO(benh): move this into 'ClientWriter' once we figure out how
  // to get a '::grpc::ClientAsyncWriterInterface' from our
  // '::grpc::ClientAsyncReaderWriter'.
  auto WritesDone() {
    return Eventual<void>()
        .raises<std::runtime_error>()
        .start(
            [this, callback = Callback<bool>()](auto& k) mutable {
              callback = [&k](bool ok) mutable {
                if (ok) {
                  k.Start();
                } else {
                  k.Fail(std::runtime_error("Failed to do 'WritesDone()'"));
                }
              };

              EVENTUALS_GRPC_LOG(1)
                  << "Writing done for call (" << context_ << ")"
                  << " with host = " << host_.value_or("*")
                  << " with path = " << path_;

              stream_->WritesDone(&callback);
            });
  }

  auto Finish() {
    struct Data {
      ::grpc::Status status;
      void* k = nullptr;
    };

    return Eventual<::grpc::Status>()
        .raises<std::runtime_error>()
        .start(
            [this,
             data = Data{},
             callback = Callback<bool>()](auto& k, auto&&...) mutable {
              using K = std::decay_t<decltype(k)>;
              data.k = &k;
              callback = [&data](bool ok) {
                auto& k = *reinterpret_cast<K*>(data.k);
                if (ok) {
                  k.Start(std::move(data.status));
                } else {
                  k.Fail(std::runtime_error("Failed to finish"));
                }
              };

              EVENTUALS_GRPC_LOG(1)
                  << "Finishing call (" << context_ << ")"
                  << " with host = " << host_.value_or("*")
                  << " with path = " << path_;

              stream_->Finish(&data.status, &callback);
            });
  }

 private:
  // TODO(benh): explicitly borrow these for better safety (they come
  // from 'Client::Call()' and outlive this 'ClientCall').
  const std::string& path_;
  const std::optional<std::string>& host_;

  ::grpc::ClientContext* context_;

  // NOTE: we need to keep this around until after the call terminates
  // as it represents a "lease" on this completion queue that once
  // relinquished will allow another call to use this queue.
  stout::borrowed_ptr<::grpc::CompletionQueue> cq_;

  ::grpc::TemplatedGenericStub<RequestType_, ResponseType_> stub_;

  std::unique_ptr<
      ::grpc::ClientAsyncReaderWriter<
          RequestType_,
          ResponseType_>>
      stream_;

  ClientReader<ResponseType_> reader_;
  ClientWriter<RequestType_> writer_;
};

////////////////////////////////////////////////////////////////////////

class Client {
 public:
  Client(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      stout::borrowed_ptr<CompletionPool> pool)
    : channel_(::grpc::CreateChannel(target, credentials)),
      pool_(std::move(pool)) {}

  auto Context() {
    return Eventual<::grpc::ClientContext*>()
        .context(eventuals::Lazy<::grpc::ClientContext>())
        .start([](auto& context, auto& k) {
          k.Start(context.get());
        });
  }

  template <typename Service, typename Request, typename Response>
  auto Call(
      const std::string& name,
      ::grpc::ClientContext* context,
      std::optional<std::string> host = std::nullopt) {
    static_assert(
        IsService<Service>::value,
        "expecting \"service\" type to be a protobuf 'Service'");

    return Call<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        context,
        std::move(host));
  }

  template <typename Request, typename Response>
  auto Call(
      std::string name,
      ::grpc::ClientContext* context,
      std::optional<std::string> host = std::nullopt) {
    static_assert(
        IsMessage<Request>::value,
        "expecting \"request\" type to be a protobuf 'Message'");

    static_assert(
        IsMessage<Response>::value,
        "expecting \"response\" type to be a protobuf 'Message'");

    using Traits = RequestResponseTraits;
    using RequestType = typename Traits::Details<Request>::Type;
    using ResponseType = typename Traits::Details<Response>::Type;

    struct Data {
      ::grpc::ClientContext* context;
      std::string name;
      std::string path;
      std::optional<std::string> host;
      stout::borrowed_ptr<::grpc::CompletionQueue> cq;
      ::grpc::TemplatedGenericStub<RequestType, ResponseType> stub;
      std::unique_ptr<
          ::grpc::ClientAsyncReaderWriter<
              RequestType,
              ResponseType>>
          stream;
      void* k = nullptr;
    };

    return Eventual<ClientCall<Request, Response>>()
        .template raises<std::runtime_error>()
        .start(
            [data = Data{
                 context,
                 std::move(name),
                 std::string(),
                 std::move(host),
                 pool_->Schedule(),
                 ::grpc::TemplatedGenericStub<
                     RequestType,
                     ResponseType>(channel_)},
             callback = Callback<bool>()](auto& k) mutable {
              const auto* method =
                  google::protobuf::DescriptorPool::generated_pool()
                      ->FindMethodByName(data.name);

              if (method == nullptr) {
                k.Fail(std::runtime_error(
                    "Method " + data.name + " not found"));
              } else {
                auto error = Traits::Validate<Request, Response>(method);
                if (error) {
                  k.Fail(std::runtime_error(error->message));
                } else {
                  if (data.host) {
                    data.context->set_authority(data.host.value());
                  }

                  data.path = "/" + data.name;
                  size_t index = data.path.find_last_of(".");
                  data.path.replace(index, 1, "/");

                  EVENTUALS_GRPC_LOG(1)
                      << "Preparing call (" << data.context << ")"
                      << " with host = " << data.host.value_or("*")
                      << " with path = " << data.path;

                  data.stream = data.stub.PrepareCall(
                      data.context,
                      data.path,
                      data.cq.get());

                  if (!data.stream) {
                    EVENTUALS_GRPC_LOG(1)
                        << "Failed to prepare call (" << data.context << ")"
                        << " with host = " << data.host.value_or("*")
                        << " with path = " << data.path;

                    // TODO(benh): Check status of channel, is this a
                    // redundant check because 'PrepareCall' also does
                    // this?  At the very least we'll probably give a
                    // better error message by checking.
                    k.Fail(std::runtime_error("Failed to prepare call"));
                  } else {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    callback = [&data](bool ok) {
                      auto& k = *reinterpret_cast<K*>(data.k);
                      if (ok) {
                        EVENTUALS_GRPC_LOG(1)
                            << "Started call (" << data.context << ")"
                            << " with host = " << data.host.value_or("*")
                            << " with path = " << data.path;

                        k.Start(
                            ClientCall<Request, Response>(
                                data.path,
                                data.host,
                                data.context,
                                std::move(data.cq),
                                std::move(data.stub),
                                std::move(data.stream)));
                      } else {
                        EVENTUALS_GRPC_LOG(1)
                            << "Failed to start call (" << data.context << ")"
                            << " with host = " << data.host.value_or("*")
                            << " with path = " << data.path;

                        k.Fail(std::runtime_error("Failed to start call"));
                      }
                    };

                    EVENTUALS_GRPC_LOG(1)
                        << "Starting call (" << data.context << ")"
                        << " with host = " << data.host.value_or("*")
                        << " with path = " << data.path;

                    data.stream->StartCall(&callback);
                  }
                }
              }
            });
  }

  template <typename Service, typename Request, typename Response>
  auto Call(
      const std::string& name,
      std::optional<std::string> host = std::nullopt) {
    static_assert(
        IsService<Service>::value,
        "expecting \"service\" type to be a protobuf 'Service'");

    return Call<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        std::move(host));
  }

  template <typename Request, typename Response>
  auto Call(
      std::string name,
      std::optional<std::string> host = std::nullopt) {
    return Context()
        | Then([this,
                name = std::move(name),
                host = std::move(host)](
                   ::grpc::ClientContext* context) mutable {
             return Call<Request, Response>(
                 std::move(name),
                 context,
                 std::move(host));
           });
  }

 private:
  std::shared_ptr<::grpc::Channel> channel_;
  stout::borrowed_ptr<CompletionPool> pool_;
};

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
