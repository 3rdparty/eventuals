#include "test/grpc/helloworld.eventuals.hh"

#include "eventuals/concurrent.hh"
#include "eventuals/do-all.hh"
#include "eventuals/finally.hh"
#include "eventuals/grpc/server.hh"
#include "eventuals/just.hh"
#include "eventuals/let.hh"
#include "eventuals/loop.hh"
#include "eventuals/map.hh"
#include "eventuals/task.hh"
#include "eventuals/then.hh"

using eventuals::Concurrent;
using eventuals::DoAll;
using eventuals::Finally;
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
        >> Finally([&](auto&& expected) {
             if (!expected.has_value()) {
               try {
                 std::rethrow_exception(expected.error());
               } catch (std::exception& e) {
                 LOG(WARNING) << "Failed to serve: " << e.what();
               } catch (...) {
                 LOG(WARNING)
                     << "Failed to serve (unexpected error that "
                     << "does not extend from 'std::exception')";
               }
             }
           });
  };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace helloworld

////////////////////////////////////////////////////////////////////////
