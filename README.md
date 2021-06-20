# `stout::grpc`

An asynchronous interface for gRPC based on callbacks. While gRPC already provides an asynchronous [interface](https://grpc.io/docs/languages/cpp/async), it is quite low-level. gRPC has an experimental (as of 2020/07/10, still [in progress](https://github.com/grpc/grpc/projects/12)) higher-level asynchronous interface based on a [reactor pattern](https://grpc.github.io/grpc/cpp/classgrpc__impl_1_1_server_bidi_reactor.html), it is still based on inheritance and overriding virtual functions. Moreover, it's still relatively low-level, e.g., it only permits a single write at a time, requiring users to buffer writes on their own.

`stout-grpc` is intended to be a higher-level inteface while still being asynchronous. It's design favors lambdas, as [Usage](#usage) below should make clear. It deliberately tries to mimic `grpc` naming where appropriate to simplify understanding across interfaces.

Please see [Known Limitations](#known-limitations) and [Suggested Improvements](#suggested-improvements) below for more insight into the limits of the inteface.

### Contact

Please reach out to `stout@3rdparty.dev` for any questions or if you're looking for support.

## Examples

Examples can be found [here](https://github.com/3rdparty/stout-grpc-examples).

They have been put in a separate repository to make it easier to clone that repository and start building a project rather than trying to figure out what pieces of the build should be copied.

We recommend cloning the examples and building them in order to play around with the library.

## Building/Testing

We suggest starting with the examples above, as they provide a good template for how you might embed this library into your own project.

Currently we only support [Bazel](https://bazel.build) and expect/use C++14 (some work could likely make this C++11).

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

[glog](https://github.com/google/glog) is used to perform logging. You'll need to enable glog verbose logging by setting the environment variable `GLOG_v=1` (or any value greater than 1) as well as the enironment variable `STOUT_GRPC_LOG=1`. You can call `google::InitGoogleLogging(argv[0]);` in your own `main()` function to properly initialize glog.

## Usage

### Server

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

Each new call accepted by the server invokes the callback passed to `Serve()`. The `call` argument is a `std::unique_ptr` but with a custom deleter so we can extend the lifetime of the `call` argument to ensure it won't get deleted until after the call has finished even if you don't hold on to the `std::unique_ptr` yourself.

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

Responses are written via 'Write()', and a call is finished via 'Finish()'. Below is a complete example of the `RecordRoute` method of the `RouteGuide` service:

> The `Write*()` family of functions all have an overload version that takes a callback/lambda that will get called to indicaute whether or not the write succeed (i.e., is going to the wire). Because each write gets queued this can be useful for determining when the write has actually been sent.

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

There is a "syntactic sugar" overload of `Serve()` that sets up the `OnRead` and an `OnDone` handler (which gets invoked when a call has finished). Here's an example of the `ListFeatures` method from `RouteGuide` which uses one of the overloadsf of `Serve()`:

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

### Client

The client side of the library uses (`stout-eventuals`)[[https://github.com/3rdparty/stout-eventuals] (eventually the server side will too).

Construct a client similar to how you might construct a channel with gRPC, except you also need a `CompletionPool`:

```cpp
using stout::eventuals::grpc;

borrowable<CompletionPool> pool;
Client client("localhost:50051", grpc::InsecureChannelCredentials(), pool.borrow());
```

You then build a call with:

```cpp
auto call = client.Call<RouteGuide, Stream<Point>, RouteSummary>("RecordRoute")
  | (Handler<grpc::Status>()
     .ready([](auto& call) {
       // Ready to write. 
     })
     .body([](auto& call, auto&& response) {
       // Read a response, when 'response' is no longer
       // borrowed another response will attempt to be read.
     }));
```

With `stout-eventuals` you need to *start* the call explicitly. In tests you can use the`*` operator, but see `stout-eventuals` for more information about starting and running asynchronous computations.

The `response` argument  in the `.body()` callback is a `stout::borrowed_ptr`. As long as `response` remains borrowed no other responses will be read from the stream. This is to ensure explicit flow control and back pressure.

You write requests via the `Write*()` family of functions.

## API Reference

### Server

```
+-----------------------------------------------+----------------------------+--------------------------------------+
|                    Function                   |         Description        |                Status                |
+-----------------------------------------------+----------------------------+--------------------------------------+
|                                               | Serves RPC at              | ServerStatus::Ok() on success,       |
|                                               | "/package.Service/Method"  | otherwise ServerStatus::Error()      |
| server->Serve<Service, Request, Response>(    | for host/authority "host"  | either due to an invalid RPC         |
|   "Method",                                   | with the specified         | method (e.g., service and/or method  |
|   "host",                                     | callback. The 'call'       | doesn't exist, incorrect             |
|   [](auto&& call) {                           | argument is a              | request/response types, endpoint     |
|     // ...                                    | 'std::unique_ptr'.         | already being served, etc).          |
|   });                                         |                            |                                      |
|                                               | You can omit host and it   |                                      |
|                                               | defaults to "*".           |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
| server->Serve<Request, Response>(             | Same as above but without  | See above.                           |
|   "package.Service.Method",                   | the 'Service' type;        |                                      |
|   [](auto&& call) {                           | all you need are the       |                                      |
|     // ...                                    | generated protobuf         |                                      |
|   });                                         | headers!                   |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
| server->Serve<Request, Response>(             | Overload that takes the    | See above.                           |
|   "package.Service.Method",                   | 'OnRead()' and 'OnDone()'  |                                      |
|   [](auto* call, auto&& request) {            | handlers and sets them     |                                      |
|     // OnRead                                 | up automagically.          |                                      |
|   },                                          |                            |                                      |
|   [](auto* call, bool cancelled) {            | Note: this overload never  |                                      |
|     // OnDone                                 | has access to the          |                                      |
|   });                                         | 'std::unique_ptr'.         |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
| call->OnRead([](auto* call, auto&& request) { | Starts reading requests.   | ServerCallStatus::Ok on              |
|   if (request) {                              |                            | success, otherwise the               |
|     // Received a request.                    |                            | call likely needs to be              |
|   } else {                                    |                            | cancelled.                           |
|     // End of stream or broken stream.        |                            |                                      |
|   }                                           |                            |                                      |
| });                                           |                            |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
|                                               | Invoked when a call has    | Always ServerCallStatus::Ok.         |
|                                               | finished. Note that 'call' |                                      |
|                                               | should not be used after   |                                      |
| call->OnDone([](auto* call, bool cancelled) { | your handler is invoked    |                                      |
|   // ...                                      | unless you haven't yet     |                                      |
| });                                           | released     the           |                                      |
|                                               | unique_ptr from the        |                                      |
|                                               | initial call.              |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
| auto status = grpc::Status::OK;               | Writes a response to the   | ServerCallStatus::Ok means           |
|                                               | client and finishes the    | the response has been                |
| call->WriteAndFinish(response, status);       | call with the specified    | queued to go out on the              |
|                                               | status. Note that this is  | wire, but has not yet been           |
| // ...                                        | the only available at      | sent.                                |
|                                               | compile time for RPCs with |                                      |
| auto options = grpc::WriteOptions();          | a unary response.          | ServerCallStatus::WritingUnavailable |
|                                               |                            | means that writing is no longer      |
| call->WriteAndFinish(                         |                            | available, likely due to a           |
|     response,                                 |                            | cancelled call or broken stream.     |
|     options,                                  |                            |                                      |
|     status);                                  |                            |                                      |
|                                               | NOTE: all Write*()         |                                      |
| call->WriteAndFinish(                         | functions have an overload |                                      |
|     response,                                 | that takes a callback      |                                      |
|     options, // Can be omitted.               | which will be invoked      |                                      |
|     [](bool ok) {                             | to indicate if the write   |                                      |
|       if (ok) {                               | succeeded or failed.       |                                      |
|         // Write succeeded.                   |                            |                                      |
|       } else {                                |                            |                                      |
|         // Write failed.                      |                            |                                      |
|       }                                       |                            |                                      |
|     },                                        |                            |                                      |
|     status);                                  |                            |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
| call->Write(response);                        | Writes a response with     | ServerCallStatus::Ok on success.     |
|                                               | optional options. Only     |                                      |
| // ...                                        | available at compile time  | See further discussion above in      |
|                                               | for server streaming RPCs. | 'WriteAndFinish()'.                  |
| auto options = grpc::WriteOptions();          |                            |                                      |
|                                               |                            |                                      |
| call->Write(response, options);               |                            |                                      |
|                                               |                            |                                      |
| call->Write(                                  |                            |                                      |
|     response,                                 |                            |                                      |
|     options, // Can be omitted.               |                            |                                      |
|     [](bool ok) {                             |                            |                                      |
|       if (ok) {                               |                            |                                      |
|         // Write succeeded.                   |                            |                                      |
|       } else {                                |                            |                                      |
|         // Write failed.                      |                            |                                      |
|       }                                       |                            |                                      |
|                                               |                            |                                      |
|     });                                       |                            |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
| call->WriteLast(response);                    | Writes a response and sets | ServerCallStatus::Ok on success.     |
|                                               | the bit indicating this    |                                      |
| // ...                                        | is the last response the   | Any subsequent calls to a 'Write*()' |
|                                               | server will send. Only     | variant will return                  |
| auto options = grpc::WriteOptions();          | available at compile time  | ServerCallStatus::WaitingForFinished |
|                                               | for server streaming RPCs. | after doing a 'WriteLast()'          |
| call->WriteLast(response, options);           |                            | because the only valid call is       |
|                                               |                            | 'Finish()' at this point.            |
| call->WriteLast(                              |                            |                                      |
|     response,                                 |                            |                                      |
|     options, // Can be omitted.               |                            |                                      |
|     [](bool ok) {                             |                            |                                      |
|       if (ok) {                               |                            |                                      |
|         // Write succeeded.                   |                            |                                      |
|       } else {                                |                            |                                      |
|         // Write failed.                      |                            |                                      |
|       }                                       |                            |                                      |
|                                               |                            |                                      |
|     });                                       |                            |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
|                                               | Finishes the call with     | ServerCallStatus::Ok on success.     |
| auto status = grpc::Status::OK;               | the specified status.      |                                      |
|                                               |                            | If the call is already done,         |
| call->Finish(status);                         |                            | e.g., because it was cancelled,      |
|                                               |                            | it may return                        |
|                                               |                            | ServerCallStatus::Done.              |
+-----------------------------------------------+----------------------------+--------------------------------------+
|                                               | Attempts to cancel         | Returns void.                        |
|                                               | the call. If successful    |                                      |
| call->context()->TryCancel();                 | any 'OnDone()' handlers    |                                      |
|                                               | will be invoked with       |                                      |
|                                               | 'cancelled' set to true.   |                                      |
+-----------------------------------------------+----------------------------+--------------------------------------+
```

### Client

*... work in progress ...*

## Known Limitations

* Services can not (yet) be removed after they are "added" via `Server::Serve()`.

* One of the key design requirements was being able to add a "service" dynamically, i.e., after the server has started, by calling `Server::Serve()`. This doesn't play nicely with some built-in components of gRPC, such as server reflection (see below). In the short-term we'd like to support adding services **before** the server starts that under the covers use `RegisterService()` so that those services can benefit from any built-in components of gRPC.

* Server Reflection via the (gRPC Server Reflection Protocol)[https://github.com/grpc/grpc/blob/master/doc/server-reflection.md] ***requires*** that all services are registered before the server starts. Because `stout::grpc::Server` is designed to allow services to be added dynamically via invoking `Server::Serve()` at any point during runtime, the reflection server started via `grpc::reflection::InitProtoReflectionServerBuilderPlugin()` will not know about any of the services. One possibility is to build a new implementation of the reflection server that works with dynamic addition/removal of services. A short-term possibility is to only support server reflection for services added before the server starts.

* No check is performed that all methods of a particular service are added via `Server::Serve()`. In practice, this probably won't be an issue as a `grpc::UNIMPLEMENTED` will get returned which is similar to how a lot of services get implemented incrementally (i.e., they implement one method at a time and return a `grpc::UNIMPLEMENTED` until they get to said method).

## Suggested Improvements

* Provide an overload of `Server::Serve()` that doesn't take a "done" calback.

* Provide a callback, e.g., an extra callback in `Server::Serve()`, that is invoked to check the health of a service or service endpoint (method/host). If nothing else, because services can be added dynamically and _after_ a server has been started this will let a router/proxy be able to do the right thing to determine "readiness".
