# `stout::grpc`

An asynchronous interface for gRPC based on callbacks. While gRPC already provides an asynchronous [interface](https://grpc.io/docs/languages/cpp/async), it is quite low-level. gRPC has an experimental (as of 2020/07/10, still [in progress](https://github.com/grpc/grpc/projects/12)) higher-level asynchronous interface based on a [reactor pattern](https://grpc.github.io/grpc/cpp/classgrpc__impl_1_1_server_bidi_reactor.html), it is still based on inheritance and overriding virtual functions. Moreover, it's still relatively low-level, e.g., it only permits a single write at a time, requiring users to buffer writes on their own.

`stout-grpc` is intended to be a higher-level inteface while still being asynchronous. It's design favors lambdas, as [Usage](#usage) below should make clear. It deliberately tries to mimic `grpc` naming where appropriate to simplify understanding across interfaces.

Please see [Known Limitations](#known-limitations) and [Suggested Improvements](#suggested-improvements) below for more insight into the limits of the inteface.

## Usage

Build a server using a `ServerBuilder` just like with gRPC:

```cpp
stout::grpc::ServerBuilder builder;
builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
```

Unlike with gRPC, `BuildAndStart()` performs validation and returns a "named-tuple" with a `status` and `server` field to better handle errors:

```cpp
auto build = builder.BuildAndStart();

if (build.status.ok()) {
  std::unique_ptr<stout::grpc::Server> server = std::move(build.server);
  // ...
} else {
  std::cerr << "Failed to build server: " << build.status.error() << std::endl;
  // ...
}
```

Unlike with gRPC, services, and in particular, endpoints, are added dynamically at any point *after* a server has started using `Server::Serve()`. An endpoint is identified by a service name, a method name, a request type, a response type, and an optional host. An endpoint is validated to ensure the specified service has the specified method with the specified request and response type. The `Stream` type is used to decorate a request as streaming. For example, to serve the `ListFeatures` method of the `RouteGuide` service which takes a `Rectangle` as a request and streams `Feature` as the response:

```cpp
auto status = server->Serve<RouteGuide, Stream<RouteNote>, Stream<RouteNote>>(
    "RouteChat",
    [](auto&& call) {
      // ...
    });
```

An invalid endpoint, e.g., because the service does not have the specified method or the request/response type is incorrect,  will return an errorful status.

An endpoint can be added for a specific host:

```cpp
auto status = server->Serve<RouteGuide, Stream<RouteNote>, Stream<RouteNote>>(
    "RouteChat",
    "host.domain.com",
    [](auto&& call) {
      // ...
    });
```

By default a host of "*" implies all possible hosts.

Each new call accepted by the server invokes the callback passed to `Serve()`. The `call` argument is a `stout::borrowed_ptr`. See [here](https://github.com/3rdparty/stout-borrowed-ptr) for more information on a `borrowed_ptr`. The salient point of a `borrowed_ptr` is it helps to extend the lifetime of the `call` argument to ensure it won't get deleted even after the call has finished.

You can access the `grpc::ServerContext` by invoking the `context()` function, for example, to determine if the call has been cancelled:

```cpp
if (call->context()->IsCancelled()) {
  // ...
}
```

Requests are read via an `OnRead` handler:

```cpp
auto status = server->Serve<RouteGuide, Stream<RouteNote>, Stream<RouteNote>>(
    "RouteChat",
    "host.domain.com",
    [](auto&& call) {
      call->OnRead([](auto* call, auto&& request) {
        if (request) {
          // ...
        } else {
          // Last request of the stream or stream broken.
        }
      });
    });
```

Responses are written via 'Write()', and a call is finished via 'Finish()'. Here's a complete example of the `RecordRoute` method of the `RouteGuide` service:

```cpp
auto status = server->Serve<RouteGuide, Stream<Point>, RouteSummary>(
    "RecordRoute",
    [this](auto&& call) {
      int point_count = 0;
      int feature_count = 0;
      float distance = 0.0;
      Point previous;

      system_clock::time_point start_time = system_clock::now();

      call->OnRead(
          [this, point_count, feature_count, distance, previous, start_time](
              auto* call, auto&& point) mutable {
            if (point) {
              point_count++;
              if (!GetFeatureName(*point, feature_list_).empty()) {
                feature_count++;
              }
              if (point_count != 1) {
                distance += GetDistance(previous, *point);
              }
              previous = *point;
            } else {
              system_clock::time_point end_time = system_clock::now();
              RouteSummary summary;
              summary.set_point_count(point_count);
              summary.set_feature_count(feature_count);
              summary.set_distance(static_cast<long>(distance));
              auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                  end_time - start_time);
              summary.set_elapsed_time(secs.count());
              call->WriteAndFinish(summary, Status::OK);
            }
          });
        });
```

In most cases one can use the higher-level version of `Serve()` that sets up the `OnRead` and `OnDone` handler automagically. Here's an example of the `ListFeatures` method from `RouteGuide`:

```cpp
auto status = server->Serve<RouteGuide, Rectangle, Stream<Feature>>(
    "ListFeatures",
    // OnRead:
    [this](auto* call, auto&& rectangle) {
      auto lo = rectangle->lo();
      auto hi = rectangle->hi();
      long left = (std::min)(lo.longitude(), hi.longitude());
      long right = (std::max)(lo.longitude(), hi.longitude());
      long top = (std::max)(lo.latitude(), hi.latitude());
      long bottom = (std::min)(lo.latitude(), hi.latitude());
      for (const Feature& f : feature_list_) {
        if (f.location().longitude() >= left &&
            f.location().longitude() <= right &&
            f.location().latitude() >= bottom &&
            f.location().latitude() <= top) {
          call->Write(f);
        }
      }
      call->Finish(Status::OK);
    },
    // OnDone:
    [](auto* call, bool cancelled) {
      assert(call->context()->IsCancelled() == cancelled);
    });
```

... to be continued ...

## Known Limitations

* Services can not (yet) be removed after they are "added" via `Server::Serve()`.

* One of the key design requirements was being able to add a "service" dynamically, i.e., after the server has started, by calling `Server::Serve()`. This doesn't play nicely with some built-in components of gRPC, such as server reflection (see below). In the short-term we'd like to support adding services **before** the server starts that under the covers use `RegisterService()` so that those services can benefit from any built-in components of gRPC.

* Server Reflection via the (gRPC Server Reflection Protocol)[https://github.com/grpc/grpc/blob/master/doc/server-reflection.md] ***requires*** that all services are registered before the server starts. Because `stout::grpc::Server` is designed to allow services to be added dynamically via invoking `Server::Serve()` at any point during runtime, the reflection server started via `grpc::reflection::InitProtoReflectionServerBuilderPlugin()` will not know about any of the services. One possibility is to build a new implementation of the reflection server that works with dynamic addition/removal of services. A short-term possibility is to only support server reflection for services added before the server starts.

* No check is performed that all methods of a particular service are added via `Server::Serve()`. In practice, this probably won't be an issue as a `grpc::UNIMPLEMENTED` will get returned which is similar to how a lot of services get implemented incrementally (i.e., they implement one method at a time and return a `grpc::UNIMPLEMENTED` until they get to said method).

## Suggested Improvements

* Provide an overload of `Server::Serve()` that doesn't take a "done" calback.

* Provide a callback, e.g., an extra callback in `Server::Serve()`, that is invoked to check the health of a service or service endpoint (method/host). If nothing else, because services can be added dynamically and _after_ a server has been started this will let a router/proxy be able to do the right thing to determine "readiness".

* Provide a callback for each variant of 'Write()' to inform when the actual data is going to the wire.