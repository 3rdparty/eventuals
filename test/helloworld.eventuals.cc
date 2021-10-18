#include "test/helloworld.eventuals.h"

#include "stout/grpc/server.h"
#include "stout/just.h"
#include "stout/let.h"
#include "stout/loop.h"
#include "stout/map.h"
#include "stout/task.h"
#include "stout/then.h"

using stout::Undefined;

using stout::eventuals::Just;
using stout::eventuals::Let;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Task;
using stout::eventuals::Then;

////////////////////////////////////////////////////////////////////////

namespace helloworld {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Concurrent(F f) {
  return f();
}

////////////////////////////////////////////////////////////////////////

Task<Undefined> Greeter::TypeErasedService::Serve() {
  return [this]() {
    return server()
               .Accept<
                   helloworld::Greeter,
                   helloworld::HelloRequest,
                   helloworld::HelloReply>("SayHello")
        | Concurrent([this]() {
             return Map(Then(Let([this](auto& call) {
               return UnaryPrologue(call)
                   | Then(Let([&](auto& request) {
                        return Then(
                            [this,
                             args = std::tuple{
                                 this, // Downcasted in 'TypeErasedSayHello()'.
                                 call.context(),
                                 &request}]() mutable {
                              return TypeErasedSayHello(&args);
                            });
                      }))
                   | UnaryEpilogue(call);
             })));
           })
        | Loop()
        | Just(Undefined());
  };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace helloworld

////////////////////////////////////////////////////////////////////////
