#pragma once

#include <thread>

#include "google/protobuf/descriptor.h"

#include "grpcpp/create_channel.h"
#include "grpcpp/completion_queue.h"

#include "grpcpp/generic/generic_stub.h"

#include "stout/borrowed_ptr.h"
#include "stout/notification.h"

#include "stout/grpc/client-call.h"
#include "stout/grpc/traits.h"

namespace stout {
namespace grpc {

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
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials);

  ~Client();

  void Shutdown();

  void Wait();

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
