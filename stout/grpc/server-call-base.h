#pragma once

#include "absl/base/call_once.h"

#include "absl/synchronization/mutex.h"

#include "absl/types/optional.h"

#include "grpcpp/generic/async_generic_service.h"

#include "stout/notification.h"

#include "stout/grpc/call-base.h"
#include "stout/grpc/call-type.h"
#include "stout/grpc/server-call-status.h"
#include "stout/grpc/server-context.h"

namespace stout {
namespace grpc {

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
  template <typename Response, typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::WriteOptions& options,
      Callback&& callback,
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
        auto status = write(&buffer, options, callback);
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
      const ::grpc::WriteOptions& options,
      const ::grpc::Status& finish_status)
  {
    return WriteAndFinish(
        response,
        options,
        std::function<void(bool)>(),
        finish_status);
  }

  template <typename Response, typename Callback>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      Callback&& callback,
      const ::grpc::Status& finish_status)
  {
    return WriteAndFinish(
        response,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback),
        finish_status);
  }

  template <typename Response>
  ServerCallStatus WriteAndFinish(
      const Response& response,
      const ::grpc::Status& finish_status)
  {
    return WriteAndFinish(
        response,
        ::grpc::WriteOptions(),
        std::function<void(bool)>(),
        finish_status);
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

  template <typename Response, typename Callback>
  ServerCallStatus Write(
      const Response& response,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions(),
      Callback&& callback = std::function<void(bool)>())
  {
    ::grpc::ByteBuffer buffer;

    if (!serialize(response, &buffer)) {
      return ServerCallStatus::FailedToSerializeResponse;
    }

    absl::MutexLock lock(&mutex_);

    if (status_ != ServerCallStatus::Ok) {
      return status_;
    }

    return write(&buffer, options, std::forward<Callback>(callback));
  }

  template <typename Response, typename Callback>
  ServerCallStatus Write(
      const Response& response,
      Callback&& callback)
  {
    return Write(
        response,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

  template <typename Response, typename Callback>
  ServerCallStatus WriteLast(
      const Response& response,
      const ::grpc::WriteOptions& options = ::grpc::WriteOptions(),
      Callback&& callback = std::function<void(bool)>())
  {
    absl::MutexLock lock(&mutex_);

    if (status_ == ServerCallStatus::Ok) {
      status_ = ServerCallStatus::WritingLast;
      auto status = write(response, options, std::forward<Callback>(callback));
      if (status == ServerCallStatus::Ok) {
        status_ = ServerCallStatus::WaitingForFinish;
      }
      return status;
    }

    return status_;
  }

  template <typename Response, typename Callback>
  ServerCallStatus WriteLast(
      const Response& response,
      Callback&& callback)
  {
    return WriteLast(
        response,
        ::grpc::WriteOptions(),
        std::forward<Callback>(callback));
  }

private:
  friend class Server;

  template <typename F>
  void OnDoneDoneDone(F&& f)
  {
    donedonedone_.Watch(f);
  }

  template <typename Callback>
  ServerCallStatus write(
      ::grpc::ByteBuffer* buffer,
      const ::grpc::WriteOptions& options,
      Callback&& callback)
    EXCLUSIVE_LOCKS_REQUIRED(mutex_)
  {
    if (!write_callback_) {
      return ServerCallStatus::WritingUnavailable;
    }

    write_datas_.emplace_back();

    auto& data = write_datas_.back();

    data.buffer.Swap(buffer);
    data.options = options;
    data.callback = std::forward<Callback>(callback);

    if (write_datas_.size() == 1) {
      buffer = &write_datas_.front().buffer;
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

  struct WriteData
  {
    ::grpc::ByteBuffer buffer;
    ::grpc::WriteOptions options;
    std::function<void(bool)> callback;
  };

  std::list<WriteData> write_datas_;

  std::function<void(bool, void*)> finish_callback_;
  absl::optional<::grpc::Status> finish_status_ = absl::nullopt;

  std::unique_ptr<ServerContext> context_;

  Notification<bool> done_;
  Notification<bool> donedonedone_;

  CallType type_;
};

} // namespace grpc {
} // namespace stout {
