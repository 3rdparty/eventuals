#include "test/helloworld.eventuals.h"

#include "eventuals/concurrent.h"
#include "eventuals/do-all.h"
#include "eventuals/grpc/server.h"
#include "eventuals/just.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/task.h"
#include "eventuals/then.h"

using eventuals::Concurrent;
using eventuals::DoAll;
using eventuals::Just;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Task;
using eventuals::Then;

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
               | Concurrent([this]() {
                   return Map(Let([this](auto& call) {
                     return UnaryPrologue(call)
                         | Then(Let([&](auto& request) {
                              return Then(
                                  [this,
                                   // NOTE: using a tuple because need
                                   // to pass more than one
                                   // argument. Also 'this' will be
                                   // downcasted appropriately in
                                   // 'TypeErasedSayHello()'.
                                   args = std::tuple{
                                       this,
                                       call.context(),
                                       &request}]() mutable {
                                    return TypeErasedSayHello(&args);
                                  });
                            }))
                         | UnaryEpilogue(call);
                   }));
                 })
               | Loop())
        | Just(); // Return 'void'.
  };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace helloworld

////////////////////////////////////////////////////////////////////////
