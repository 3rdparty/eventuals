#pragma once

#include "grpcpp/client_context.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/create_channel.h"

#include "grpcpp/generic/generic_stub.h"

#include "stout/borrowable.h"
#include "stout/callback.h"
#include "stout/eventual.h"

#include "stout/eventuals/grpc/completion-pool.h"
#include "stout/eventuals/grpc/handler.h"

#include "stout/grpc/traits.h"

namespace stout {
namespace eventuals {
namespace grpc {
namespace detail {

using stout::eventuals::detail::operator|;

template <typename K_, typename Request_, typename Response_>
class Call {
public:
  using Value = typename ValueFrom<K_, Undefined>::type;

  using Traits = ::stout::grpc::RequestResponseTraits;

  using RequestType_ = typename Traits::Details<Request_>::Type;
  using ResponseType_ = typename Traits::Details<Response_>::Type;

  Call(K_ k,
       const std::string& name,
       const std::optional<std::string>& host,
       borrowed_ptr<::grpc::CompletionQueue> cq,
       ::grpc::TemplatedGenericStub<RequestType_, ResponseType_> stub)
    : k_(std::move(k)),
      name_(name),
      host_(host),
      cq_(std::move(cq)),
      stub_(std::move(stub)),
      finish_(false) {}

  Call(Call&& that)
    : k_(std::move(that.k_)),
      name_(std::move(that.name_)),
      host_(std::move(that.host_)),
      cq_(std::move(that.cq_)),
      stub_(std::move(that.stub_)),
      finish_(false) {
    // NOTE: only expecting move construction during composing.
    if (stream_) {
      throw std::runtime_error("Composing after starting");
    }
  }

  template <typename Request, typename Response, typename K, typename Stub>
  static auto create(
      K k,
      const std::string& name,
      const std::optional<std::string>& host,
      borrowed_ptr<::grpc::CompletionQueue> cq,
      Stub stub) {
    return Call<K, Request, Response>(
        std::move(k),
        std::move(name),
        std::move(host),
        std::move(cq),
        std::move(stub));
  }

  template <
    typename H,
    std::enable_if_t<
      IsHandler<H>::value, int> = 0>
  auto k(H h) && {
    static_assert(IsUndefined<K_>::value, "Duplicate handler");

    // NOTE: 'k_' may be overwritten multiple times as we compose
    // eventuals together via operator|().
    if (stream_) {
      throw std::runtime_error("Composing after starting");
    }

    auto k = create<Request_, Response_>(
        std::move(h),
        std::move(name_),
        std::move(host_),
        std::move(cq_),
        std::move(stub_));

    return stout::eventuals::detail::Eventual<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined>::create<typename H::Value>(
          std::move(k),
          Undefined(),
          [](auto& k) {
            k.Start();
            // start(k);
          },
          Undefined(),
          [](auto& k) {
            k.Stop();
            // stop(k);
          },
          Undefined());
  }

  template <
    typename K,
    std::enable_if_t<
      !IsHandler<K>::value, int> = 0>
  auto k(K k) && {
    static_assert(
        !IsTerminal<K>::value || !IsUndefined<K_>::value,
        "Missing 'Handler'");

    // NOTE: 'k_' may be overwritten multiple times as we compose
    // eventuals together via operator|().
    if (stream_) {
      throw std::runtime_error("Composing after starting");
    }

    return create<Request_, Response_>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(name_),
        std::move(host_),
        std::move(cq_),
        std::move(stub_));
  }

  template <typename... Args>
  void Start(Args&&...) {
    const auto* method = google::protobuf::DescriptorPool::generated_pool()
      ->FindMethodByName(name_);

    if (method == nullptr) {
      auto status = ::grpc::Status(
          ::grpc::INVALID_ARGUMENT,
          "method not found");
      k_.Finished(std::move(status));
    } else {
      auto error = Traits::Validate<Request_, Response_>(method);
      if (error) {
        auto status = ::grpc::Status(
            ::grpc::INVALID_ARGUMENT,
            error->message);
        k_.Finished(std::move(status));
      } else {
        if (host_) {
          context_.set_authority(host_.value());
        }

        // Let handler modify context, e.g., to set a deadline.
        k_.Prepare(context_);

        std::string path = "/" + name_;
        size_t index = path.find_last_of(".");
        path.replace(index, 1, "/");

        stream_ = stub_.PrepareCall(&context_, path, cq_.get());

        if (!stream_) {
          // TODO(benh): Check status of channel, is this a redundant
          // check because PrepareCall also does this? At the very
          // least we'll probably give a better error message by
          // checking.
          auto status = ::grpc::Status(
              ::grpc::INTERNAL,
              "GenericStub::PrepareCall returned nullptr");
          k_.Finished(std::move(status));
        } else {
          start_callback_ = [this](bool ok) {
            if (ok) {
              k_.Ready(*this);
              stream_->Read(response_.get(), &read_callback_);
            } else {
              auto status = ::grpc::Status(
                  ::grpc::UNAVAILABLE,
                  "channel is either permanently broken or transiently broken"
                  " but with the fail-fast option");
              k_.Finished(std::move(status));
            }
          };

          read_callback_ = [this](bool ok) mutable {
            if (ok) {
              auto response = response_.borrow();
              response_.watch([this]() {
                stream_->Read(response_.get(), &read_callback_);
              });
              k_.Body(*this, std::move(response));
            } else {
              // Signify end of stream (or error).
              k_.Body(*this, borrowed_ptr<ResponseType_>());
            }
          };

          write_callback_ = [this](bool ok) mutable {
            if (ok && !finish_.load()) {
              k_.Ready(*this);
            } else {
              stream_->Finish(&finish_status_, &finish_callback_);
            }
          };

          finish_callback_ = [this](bool ok) mutable {
            // Relinquish the completion queue so as to signify that
            // another call may use it.
            cq_.relinquish();

            if (ok) {
              k_.Finished(std::move(finish_status_));
            } else {
              auto status = ::grpc::Status(
                  ::grpc::INTERNAL,
                  "failed to finish");
              k_.Finished(std::move(status));
            }
          };

          stream_->StartCall(&start_callback_);

          // NOTE: we install the interrupt handler *after* we've
          // started the calls so as to avoid a race with calling
          // TryCancel() before the call has started.
          if (handler_) {
            if (!handler_->Install()) {
              handler_->Invoke();
            }
          }
        }
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args) {
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop() {
    eventuals::stop(k_);
  }

  void Register(Interrupt& interrupt) {
    k_.Register(interrupt);

    handler_.emplace(&interrupt, [this]() {
      context_.TryCancel();
    });
  }

  void Write(
      const RequestType_& request,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions()) {
    stream_->Write(request, options, &write_callback_);
  }

  void WriteLast(
      const RequestType_& request,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions()) {
    finish_.store(true);
    stream_->WriteLast(request, options, &write_callback_);
  }

  void WritesDone() {
    finish_.store(true);
    stream_->WritesDone(&write_callback_);
  }

  ::grpc::ClientContext& context() & {
    return context_;
  }

private:
  K_ k_;

  std::optional<Interrupt::Handler> handler_;

  std::string name_;
  std::optional<std::string> host_;

  // NOTE: we need to keep this around until after the call terminates
  // as it represents a "lease" on this completion queue that once
  // relinquished will allow another call to use this queue.
  borrowed_ptr<::grpc::CompletionQueue> cq_;

  ::grpc::TemplatedGenericStub<RequestType_, ResponseType_> stub_;

  ::grpc::ClientContext context_;

  std::unique_ptr<
    ::grpc::ClientAsyncReaderWriter<RequestType_, ResponseType_>> stream_;

  Callback<bool> start_callback_;
  Callback<bool> read_callback_;
  Callback<bool> write_callback_;
  Callback<bool> finish_callback_;

  borrowable<ResponseType_> response_;

  std::atomic<bool> finish_;
  ::grpc::Status finish_status_;
};

} // namespace detail {

template <typename>
struct IsCall : std::false_type {};


template <typename K, typename RequestType, typename ResponseType>
struct IsCall<
  detail::Call<K, RequestType, ResponseType>> : std::true_type {};


class Client {
public:
  Client(
      const std::string& target,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      borrowed_ptr<CompletionPool> pool)
    : channel_(::grpc::CreateChannel(target, credentials)),
      pool_(std::move(pool)) {}

  template <
    typename Service,
    typename Request,
    typename Response,
    std::enable_if_t<
      stout::grpc::IsService<Service>::value
      && stout::grpc::IsMessage<Request>::value
      && stout::grpc::IsMessage<Response>::value, int> = 0>
  auto Call(
      const std::string& name,
      const std::optional<std::string>& host = std::nullopt) {
    return Call<Request, Response>(
        std::string(Service::service_full_name()) + "." + name,
        host);
  }

  template <
    typename Request,
    typename Response,
    std::enable_if_t<
      stout::grpc::IsMessage<Request>::value
      && stout::grpc::IsMessage<Response>::value, int> = 0>
  auto Call(
      const std::string& name,
      const std::optional<std::string>& host = std::nullopt) {
    using Traits = ::stout::grpc::RequestResponseTraits;
    using RequestType = typename Traits::Details<Request>::Type;
    using ResponseType = typename Traits::Details<Response>::Type;

    return detail::Call<Undefined, Request, Response>(
        Undefined(),
        name,
        host,
        pool_->Schedule(),
        ::grpc::TemplatedGenericStub<RequestType, ResponseType>(channel_));
  }

private:
  std::shared_ptr<::grpc::Channel> channel_;
  borrowed_ptr<CompletionPool> pool_;
};

} // namespace grpc {

template <typename K, typename Request, typename Response>
struct IsContinuation<
  grpc::detail::Call<K, Request, Response>> : std::true_type {};


template <typename K, typename Request, typename Response>
struct HasTerminal<
  grpc::detail::Call<K, Request, Response>> : HasTerminal<K> {};

} // namespace eventuals {
} // namespace stout {
