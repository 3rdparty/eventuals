#pragma once

#include "absl/base/call_once.h"

#include "absl/synchronization/mutex.h"

#include "absl/types/optional.h"

#include "grpcpp/client_context.h"

#include "grpcpp/generic/generic_stub.h"

#include "stout/borrowed_ptr.h"
#include "stout/notification.h"

#include "stout/grpc/call-base.h"
#include "stout/grpc/call-type.h"
#include "stout/grpc/client-call-status.h"

namespace stout {
namespace grpc {

// Forward declarations.
class Client;


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

} // namespace grpc {
} // namespace stout {
