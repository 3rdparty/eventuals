#include "stout/grpc/server.h"

namespace stout {
namespace grpc {

ServerCallBase::ServerCallBase(std::unique_ptr<ServerContext>&& context, CallType type)
  : status_(ServerCallStatus::Ok), context_(std::move(context)), type_(type)
{
  // NOTE: we rely on the explicit design of Notification where the
  // ***first*** handler added via 'Watch()' will be the last handler
  // that gets executed in order to trigger 'donedonedone_' after
  // we've triggered all of the other 'OnDone()' handlers.
  done_.Watch([this](bool cancelled) {
    donedonedone_.Notify(cancelled);
  });

  // NOTE: 'context_->OnDone()' NOT 'this->OnDone()'.
  context_->OnDone([this](bool cancelled) {
    // NOTE: emperical evidence shows that gRPC may invoke the done
    // handler set up by 'AsyncNotifyWhenDone' before our
    // 'finish_callback_' gets invoked. That or there is a nasty race
    // between when a thread gets the 'finish_callback_' tag from the
    // completion queue and gRPC therefore decides that everything is
    // done even though that thread may not yet have invoked the
    // callback. So, if we were in the 'Finishing' state then we wait
    // for the callback to notify we're "done done done", otherwise we
    // notify now.
    mutex_.Lock();

    if (status_ != ServerCallStatus::Finishing) {
      status_ = ServerCallStatus::Done;
      mutex_.Unlock();
      done_.Notify(cancelled);
    } else {
      status_ = ServerCallStatus::Done;
      mutex_.Unlock();
    }
  });

  write_callback_ = [this](bool ok, void*) {
    if (ok) {
      ::grpc::ByteBuffer* buffer = nullptr;
      ::grpc::WriteOptions* options = nullptr;
      ::grpc::Status* finish_status = nullptr;

      mutex_.Lock();

      write_buffers_.pop_front();

      if (!write_buffers_.empty()) {
        buffer = &write_buffers_.front().first;
        options = &write_buffers_.front().second;
      } else if (finish_status_) {
        finish_status = &finish_status_.value();
      }

      mutex_.Unlock();

      if (buffer != nullptr) {
        stream()->Write(*buffer, *options, &write_callback_);
      } else if (finish_status != nullptr) {
        stream()->Finish(*finish_status, &finish_callback_);
      }
    } else {
      mutex_.Lock();
      write_callback_ = std::function<void(bool, void*)>();
      mutex_.Unlock();
    }
  };

  finish_callback_ = [this](bool, void*) {
    mutex_.Lock();
    status_ = ServerCallStatus::Done;
    mutex_.Unlock();
    done_.Notify(this->context()->IsCancelled());
  };
}


ServerCallStatus ServerCallBase::Finish(const ::grpc::Status& finish_status)
{
  auto status = ServerCallStatus::Ok;

  mutex_.Lock();

  status = status_;

  if (status == ServerCallStatus::Ok
      || status == ServerCallStatus::WaitingForFinish) {
    finish_status_ = finish_status;
    status_ = ServerCallStatus::Finishing;

    if (write_buffers_.empty()) {
      mutex_.Unlock();
      stream()->Finish(finish_status, &finish_callback_);
      return ServerCallStatus::Ok;
    }
  }

  mutex_.Unlock();

  return status;
}

} // namespace grpc {
} // namespace stout {
