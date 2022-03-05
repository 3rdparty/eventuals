#pragma once

#include <tuple>

#include "eventuals/grpc/server.h"
#include "eventuals/task.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"

namespace helloworld {
namespace eventuals {

class Greeter {
 public:
  static constexpr char const* service_full_name() {
    return helloworld::Greeter::service_full_name();
  }

  class TypeErasedService : public ::eventuals::grpc::Service {
   public:
    ::eventuals::Task::Of<void> Serve() override;

    char const* name() override {
      return Greeter::service_full_name();
    }

   protected:
    virtual ~TypeErasedService() = default;

    virtual ::eventuals::Task::Of<HelloReply> TypeErasedSayHello(
        std::tuple<
            TypeErasedService*, // this
            ::grpc::GenericServerContext*,
            HelloRequest*>* args) = 0;
  };

  template <typename Implementation>
  class Service : public TypeErasedService {
    ::eventuals::Task::Of<HelloReply> TypeErasedSayHello(
        std::tuple<
            TypeErasedService*,
            ::grpc::GenericServerContext*,
            HelloRequest*>* args) override {
      return [args]() {
        return ::eventuals::Then([args]() mutable {
          return std::apply(
              [](auto* implementation, auto* context, auto* request) {
                static_assert(std::is_base_of_v<Service, Implementation>);
                return dynamic_cast<Implementation*>(implementation)
                    ->SayHello(context, std::move(*request));
              },
              *args);
        });
      };
    }
  };
};

} // namespace eventuals
} // namespace helloworld
