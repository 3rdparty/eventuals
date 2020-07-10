#pragma once

#include "grpcpp/impl/codegen/proto_utils.h"

#include "stout/grpc/logging.h"

class CallBase
{
protected:
  // TODO(benh): Provide a shared 'write()' that implements both
  // 'ClientCallBase::write()' and 'ServerCallBase::write()'? Will
  // need to figure out how to inject the write callbacks as they are
  // different between the client and server.

  template <typename T>
  bool serialize(const T& t, ::grpc::ByteBuffer* buffer)
  {
    bool own = true;

    auto status = ::grpc::SerializationTraits<T>::Serialize(
        t,
        buffer,
        &own);

    if (status.ok()) {
      return true;
    } else {
      VLOG_IF(1, STOUT_GRPC_LOG)
        << "Failed to serialize " << t.GetTypeName()
        << ": " << status.error_message() << std::endl;
      return false;
    }
  }

  template <typename T>
  bool deserialize(::grpc::ByteBuffer* buffer, T* t)
  {
    auto status = ::grpc::SerializationTraits<T>::Deserialize(
        buffer,
        t);

    if (status.ok()) {
      return true;
    } else {
      VLOG_IF(1, STOUT_GRPC_LOG)
        << "Failed to deserialize " << t->GetTypeName()
        << ": " << status.error_message() << std::endl;
      return false;
    }
  }
};
