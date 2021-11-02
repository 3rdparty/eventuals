[![macOS](https://github.com/3rdparty/eventuals-grpc/workflows/macOS/badge.svg?branch=master)](https://github.com/3rdparty/eventuals-grpc/actions/workflows/macos.yml)
[![Ubuntu](https://github.com/3rdparty/eventuals-grpc/workflows/Ubuntu/badge.svg?branch=master)](https://github.com/3rdparty/eventuals-grpc/actions/workflows/ubuntu.yml)
[![Windows](https://github.com/3rdparty/eventuals-grpc/workflows/Windows/badge.svg?branch=master)](https://github.com/3rdparty/eventuals-grpc/actions/workflows/windows.yml)

# `eventuals::grpc`

An asynchronous interface for gRPC based on callbacks. While gRPC already provides an asynchronous [interface](https://grpc.io/docs/languages/cpp/async), it is quite low-level. gRPC has an experimental (as of 2020/07/10, still [in progress](https://github.com/grpc/grpc/projects/12)) higher-level asynchronous interface based on a [reactor pattern](https://grpc.github.io/grpc/cpp/classgrpc__impl_1_1_server_bidi_reactor.html), it is still based on inheritance and overriding virtual functions to receive "callbacks" and as such is hard to compose with other asynchronous code. Moreover, it's still relatively low-level, e.g., it only permits a single write at a time, requiring users to buffer writes on their own.

`eventuals-grpc` is intended to be a higher-level inteface while still being asynchronous that uses (`eventuals`)[[https://github.com/3rdparty/eventuals] to make composing asynchronous code easy. It's design favors lambdas, as [Usage](#usage) below should make clear. It deliberately tries to mimic `grpc` naming where appropriate to simplify understanding across interfaces.

Please see [Known Limitations](#known-limitations) and [Suggested Improvements](#suggested-improvements) below for more insight into the limits of the inteface.

## Examples

Examples can be found [here](https://github.com/3rdparty/eventuals-grpc-examples).

They have been put in a separate repository to make it easier to clone that repository and start building a project rather than trying to figure out what pieces of the build should be copied.

We recommend cloning the examples and building them in order to play around with the library.

## Building/Testing

We suggest starting with the examples above, as they provide a good template for how you might embed this library into your own project.

Currently we only support [Bazel](https://bazel.build) and expect/use C++17.

You can build the library with:

```sh
$ bazel build :grpc
...
```

You can build and run the tests with:

```sh
$ bazel test test:grpc
...
```

## Logging

[glog](https://github.com/google/glog) is used to perform logging. You'll need to enable glog verbose logging by setting the environment variable `GLOG_v=1` (or any value greater than 1) as well as the enironment variable `EVENTUALS_GRPC_LOG=1`. You can call `google::InitGoogleLogging(argv[0]);` in your own `main()` function to properly initialize glog.

## Known Limitations

* Services can not (yet) be removed after they are "added" via `Server::Accept()`.

* One of the key design requirements was being able to add a "service" dynamically, i.e., after the server has started, by calling `Server::Accept()`. This doesn't play nicely with some built-in components of gRPC, such as server reflection (see below). In the short-term we'd like to support adding services **before** the server starts that under the covers use `RegisterService()` so that those services can benefit from any built-in components of gRPC.

* Server Reflection via the (gRPC Server Reflection Protocol)[https://github.com/grpc/grpc/blob/master/doc/server-reflection.md] ***requires*** that all services are registered before the server starts. Because `eventuals::grpc::Server` is designed to allow services to be added dynamically via invoking `Server::Accept()` at any point during runtime, the reflection server started via `grpc::reflection::InitProtoReflectionServerBuilderPlugin()` will not know about any of the services. One possibility is to build a new implementation of the reflection server that works with dynamic addition/removal of services. A short-term possibility is to only support server reflection for services added before the server starts.

* No check is performed that all methods of a particular service are added via `Server::Accept()`. In practice, this probably won't be an issue as a `grpc::UNIMPLEMENTED` will get returned which is similar to how a lot of services get implemented incrementally (i.e., they implement one method at a time and return a `grpc::UNIMPLEMENTED` until they get to said method).
