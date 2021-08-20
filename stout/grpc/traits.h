#pragma once

#include <optional>
#include <string>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message_lite.h"
#include "stout/grpc/call-type.h"

namespace stout {
namespace eventuals {
namespace grpc {

// Used to "decorate" requests/responses streams.
template <typename T>
struct Stream {};


template <typename T>
class IsService {
 private:
  typedef char Yes[1];
  typedef char No[2];

  template <typename U>
  static Yes& test(decltype(&U::service_full_name));

  template <typename U>
  static No& test(...);

 public:
  enum { value = sizeof(test<T>(0)) == sizeof(Yes) };
};


template <typename T>
struct IsMessage
  : std::is_base_of<google::protobuf::MessageLite, T> {};


template <typename T>
struct IsMessage<Stream<T>>
  : std::is_base_of<google::protobuf::MessageLite, T> {};


struct RequestResponseTraits {
  struct Error {
    std::string message;
  };

  template <typename T>
  struct Details {
    static std::string name() {
      return T().GetTypeName();
    }

    using Type = T;

    constexpr static bool streaming = false;
  };

  template <typename T>
  struct Details<Stream<T>> {
    static std::string name() {
      return T().GetTypeName();
    }

    using Type = T;

    constexpr static bool streaming = true;
  };

  template <typename Request, typename Response>
  static CallType Type() {
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
  static std::optional<Error> Validate(
      const google::protobuf::MethodDescriptor* method) {
    if (Details<Request>::streaming && !method->client_streaming()) {
      return Error{"Method does not have streaming requests"};
    }

    if (!Details<Request>::streaming && method->client_streaming()) {
      return Error{"Method has streaming requests"};
    }

    if (Details<Response>::streaming && !method->server_streaming()) {
      return Error{"Method does not have streaming responses"};
    }

    if (!Details<Response>::streaming && method->server_streaming()) {
      return Error{"Method has streaming responses"};
    }

    if (Details<Request>::name() != method->input_type()->full_name()) {
      return Error{
          "Method does not have requests of type "
          + Details<Request>::name()};
    }

    if (Details<Response>::name() != method->output_type()->full_name()) {
      return Error{
          "Method does not have responses of type "
          + Details<Response>::name()};
    }

    return std::nullopt;
  }
};

} // namespace grpc
} // namespace eventuals
} // namespace stout
