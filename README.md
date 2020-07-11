# `stout::grpc`




## Known Limitations

* Services can not (yet) be removed after they are "added" via `Server::Serve()`.

* One of the key design requirements was being able to add a "service" dynamically, i.e., after the server has started, by calling `Server::Serve()`. This doesn't play nicely with some built-in components of gRPC, such as server reflection (see below). In the short-term we'd like to support adding services **before** the server starts that under the covers use `RegisterService()` so that those services can benefit from any built-in components of gRPC.

* Server Reflection via the (gRPC Server Reflection Protocol)[https://github.com/grpc/grpc/blob/master/doc/server-reflection.md] ***requires*** that all services are registered before the server starts. Because `stout::grpc::Server` is designed to allow services to be added dynamically via invoking `Server::Serve()` at any point during runtime, the reflection server started via `grpc::reflection::InitProtoReflectionServerBuilderPlugin()` will not know about any of the services. One possibility is to build a new implementation of the reflection server that works with dynamic addition/removal of services. A short-term possibility is to only support server reflection for services added before the server starts.

* No check is performed that all methods of a particular service are added via `Server::Serve()`. In practice, this probably won't be an issue as a `grpc::UNIMPLEMENTED` will get returned which is similar to how a lot of services get implemented incrementally (i.e., they implement one method at a time and return a `grpc::UNIMPLEMENTED` until they get to said method).

## Suggested Improvements

* Provide an overload of `Server::Serve()` that doesn't take a "done" calback.

* Provide a callback, e.g., an extra callback in `Server::Serve()`, that is invoked to check the health of a service or service endpoint (method/host). If nothing else, because services can be added dynamically and _after_ a server has been started this will let a router/proxy be able to do the right thing to determine "readiness".

* Provide a callback for each variant of 'Write()' to inform when the actual data is going to the wire.