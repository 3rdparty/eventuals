#include "test/grpc/helloworld.eventuals.h"

#include "eventuals/concurrent.h"
#include "eventuals/do-all.h"
#include "eventuals/expected.h"
#include "eventuals/finally.h"
#include "eventuals/grpc/server.h"
#include "eventuals/just.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/task.h"
#include "eventuals/then.h"

using eventuals::Concurrent;
using eventuals::DoAll;
using eventuals::expected;
using eventuals::Finally;
using eventuals::Just;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Stopped;
using eventuals::Task;
using eventuals::Then;
using eventuals::What;

////////////////////////////////////////////////////////////////////////

namespace helloworld {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

Task::Of<void> Greeter::TypeErasedService::Serve() {
  return [this]() {
    return DoAll(
               // SayHello
               server()
                   .Accept<
                       helloworld::Greeter,
                       helloworld::HelloRequest,
                       helloworld::HelloReply>("SayHello")
               >> Concurrent([this]() {
                   return Map(Let([this](
                                      ::eventuals::grpc::ServerCall<
                                          HelloRequest,
                                          HelloReply>& call) {
                     return UnaryPrologue(call)
                         >> Then(Let([&](HelloRequest& request) {
                              return Then(
                                  [&,
                                   // NOTE: using a tuple because need
                                   // to pass more than one
                                   // argument. Also 'this' will be
                                   // downcasted appropriately in
                                   // 'TypeErasedSayHello()'.
                                   args = std::tuple{
                                       this,
                                       call.context(),
                                       &request}]() mutable {
                                    return TypeErasedSayHello(&args)
                                        >> UnaryEpilogue(call);
                                  });
                            }));
                   }));
                 })
               >> Loop())
        >> Finally([&](expected<
                       std::tuple<std::monostate>,
                       std::variant<Stopped, std::runtime_error>>&& expected) {
             if (!expected.has_value()) {
               std::visit(
                   [](auto&& error) {
                     LOG(WARNING) << "Failed to serve: " << error.what();
                   },
                   std::move(expected.error()));
             }
           });
  };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace helloworld

////////////////////////////////////////////////////////////////////////
