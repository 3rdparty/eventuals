#pragma once

#include <optional>
#include <tuple>

#include "examples/protos/helloworld.grpc.pb.h"
#include "stout/grpc/server.h"
#include "stout/task.h"
#include "stout/then.h"

namespace helloworld {
namespace eventuals {

class Greeter {
 public:
  class TypeErasedService : public stout::eventuals::grpc::Service {
   public:
    stout::eventuals::Task<stout::Undefined> Serve() override;

    const std::string& service_full_name() override {
      static std::string name = helloworld::Greeter::service_full_name();
      return name;
    }

   protected:
    virtual ~TypeErasedService() = default;

    virtual stout::eventuals::Task<HelloReply> TypeErasedSayHello(
        std::tuple<
            TypeErasedService*, // this
            ::grpc::GenericServerContext*,
            HelloRequest*>* args) = 0;
  };

  template <typename Implementation>
  class Service : public TypeErasedService {
    stout::eventuals::Task<HelloReply> TypeErasedSayHello(
        std::tuple<
            TypeErasedService*,
            ::grpc::GenericServerContext*,
            HelloRequest*>* args) override {
      return [args]() {
        return stout::eventuals::Then([args]() mutable {
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
