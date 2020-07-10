#include "stout/grpc/client.h"

namespace stout {
namespace grpc {

ClientCallBase::ClientCallBase(CallType type) : type_(type)
{
  write_callback_ = [this](bool ok, void*) mutable {
    if (ok) {
      ::grpc::ByteBuffer* buffer = nullptr;
      ::grpc::WriteOptions* options = nullptr;
      ::grpc::Status* finish_status = nullptr;

      bool do_writes_done = false;

      mutex_.Lock();

      // Might get here after doing a 'WritesDone()' so don't have
      // anything to pop/destruct.
      if (!write_buffers_.empty()) {
        write_buffers_.pop_front();
      }

      if (!write_buffers_.empty()) {
        buffer = &write_buffers_.front().first;
        options = &write_buffers_.front().second;
      } else if (finish_status_) {
        finish_status = &finish_status_.value();
      } else if (status_ == ClientCallStatus::WaitingForFinish && !writes_done_) {
        do_writes_done = writes_done_ = true;
      } else {
        writing_ = false;
      }

      mutex_.Unlock();

      if (buffer != nullptr) {
        stream_->Write(*buffer, *options, &write_callback_);
      } else if (finish_status != nullptr) {
        stream_->Finish(finish_status, &finish_callback_);
      } else if (do_writes_done) {
        stream_->WritesDone(&write_callback_);
      }
    } else {
      mutex_.Lock();
      write_callback_ = std::function<void(bool, void*)>();
      mutex_.Unlock();
    }
  };
}


ClientCallStatus ClientCallBase::WritesDone()
{
  auto status = ClientCallStatus::Ok;

  mutex_.Lock();

  status = status_;

  if (status == ClientCallStatus::Ok) {
    status_ = ClientCallStatus::WaitingForFinish;

    if (!writing_ && write_callback_) {
      writing_ = true;
      writes_done_ = true;
      mutex_.Unlock();
      stream_->WritesDone(&write_callback_);
      return ClientCallStatus::Ok;
    }
  }

  mutex_.Unlock();

  return status;
}


ClientCallStatus ClientCallBase::Finish()
{
  mutex_.Lock();

  if (status_ == ClientCallStatus::Ok || status_ == ClientCallStatus::WaitingForFinish) {
    finish_status_ = ::grpc::Status();
    status_ = ClientCallStatus::Finishing;

    // Set up 'finish_callback_' in case it hasn't already been.
    OnFinished([](auto&&) {});

    bool writing = writing_;

    mutex_.Unlock();

    if (!writing) {
      stream_->Finish(&finish_status_.value(), &finish_callback_);
    }

    return ClientCallStatus::Ok;
  }

  auto status = status_;

  mutex_.Unlock();

  return status;
}

} // namespace grpc {
} // namespace stout {
