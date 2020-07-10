#pragma once

#include <string>

#include "absl/types/optional.h"

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message_lite.h"

#include "stout/grpc/call-type.h"

namespace stout {
namespace grpc {

// Used to "decorate" requests/responses streams.
template <typename T> struct Stream {};


template <typename T>
class IsService
{
private:
  typedef char Yes[1];
  typedef char No[2];

  template <typename U> static Yes& test(decltype(&U::service_full_name));
  template <typename U> static No& test(...);

public:
  enum { value = sizeof(test<T>(0)) == sizeof(Yes) };
};


template <typename T>
struct IsMessage
  : std::is_base_of<google::protobuf::MessageLite, T> {};


template <typename T>
struct IsMessage<Stream<T>>
  : std::is_base_of<google::protobuf::MessageLite, T> {};


struct RequestResponseTraits
{
  struct Error
  {
    std::string message;
  };

  template <typename T>
  struct Details
  {
    static std::string name()
    {
      return T().GetTypeName();
    }

    using Type = T;

    constexpr static bool streaming = false;
  };

  template <typename T>
  struct Details<Stream<T>>
  {
    static std::string name()
    {
      return T().GetTypeName();
    }

    using Type = T;

    constexpr static bool streaming = true;
  };

  template <typename Request, typename Response>
  static CallType Type()
  {
    if (Details<Request>::streaming && Details<Response>::streaming) {
      return CallType::BIDI_STREAMING;
    } else if (Details<Request>::streaming) {
      return CallType::CLIENT_STREAMING;
    } else if (Details<Response>::streaming) {
      return CallType::SERVER_STREAMING;
    } else {
      return CallType::UNARY;
    }
  }

  template <typename Request, typename Response>
  static absl::optional<Error> Validate(const google::protobuf::MethodDescriptor* method)
  {
    if (Details<Request>::streaming && !method->client_streaming()) {
      return Error { "Method DOES NOT have streaming requests" };
    }

    if (Details<Response>::streaming && !method->server_streaming()) {
      return Error { "Method DOES NOT have streaming responses" };
    }

    if (Details<Request>::name() != method->input_type()->full_name()) {
      return Error {
          "Method does not have requests of type "
          + Details<Request>::name()
      };
    }

    if (Details<Response>::name() != method->output_type()->full_name()) {
      return Error {
          "Method does not have reponses of type "
          + Details<Response>::name()
      };
    }

    return absl::nullopt;
  }
};


template <typename F, typename Call, typename T>
struct IsReadHandler
  : std::is_constructible<
      std::function<void(Call*, std::unique_ptr<typename RequestResponseTraits::Details<T>::Type>&&)>,
      F> {};


template <typename F, typename Call>
struct IsDoneHandler
  : std::is_constructible<
      std::function<void(Call*, bool)>,
      F> {};


template <typename F, typename Call>
struct IsFinishedHandler
  : std::is_constructible<
      std::function<void(Call*, const ::grpc::Status&)>,
      F> {};


template <typename F, typename Call, typename... Args>
struct IsCallHandler
  : std::is_constructible<
      std::function<void(borrowed_ptr<Call>&&, Args...)>,
      F> {};

} // namespace grpc {
} // namespace stout {
