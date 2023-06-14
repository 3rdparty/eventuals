[![macOS](https://github.com/3rdparty/eventuals/workflows/macOS/badge.svg?branch=main)](https://github.com/3rdparty/eventuals/actions/workflows/macos.yml)
[![Ubuntu](https://github.com/3rdparty/eventuals/workflows/Ubuntu/badge.svg?branch=main)](https://github.com/3rdparty/eventuals/actions/workflows/ubuntu.yml)
[![Windows](https://github.com/3rdparty/eventuals/workflows/Windows/badge.svg?branch=main)](https://github.com/3rdparty/eventuals/actions/workflows/windows.yml)

# Eventuals

A C++ library for writing asynchronous computations out of **_continuations_**.

Unlike many other approaches to asynchronous code, eventuals doesn't require locking or dynamic heap allocations by default.

**Callbacks**, one of the most common approaches to asynchronous programming, are hard to compose, don't (often) support cancellation, and are generally tricky to reason about.

**Futures/Promises** are an approach that does support composition and cancellation, but many implementations have poor performance due to locking overhead and dynamic heap allocations. Furthermore, because the evaluation model of most futures/promises libraries is **eager** referential transparency is lost.

**Eventuals** are an approach that, much like futures/promises, support composition and cancellation, however, are **lazy**. That is, an eventual has to be explicitly _started_.

Another key difference from futures/promises is that an eventual's continuation is **not** type-erased and can be directly used, saved, etc by the programmer. This allows the compiler to perform significant optimizations that are difficult to do with other lazy approaches that perform type-erasure. The tradeoff is that more code needs to be written in headers, which may lead to longer compile times. You can, however, mitgate long compile times by using a type-erasing `Task` like type (more on this later).

The library provides numerous abstractions that simplify asynchronous computations, e.g., a `Stream` for performing asynchronous streaming. Each of these abstractions follow a "builder" pattern for constructing them, see [Usage](#usage) below for more details.

This library was inspired from experience building and using the [libprocess](https://github.com/3rdparty/libprocess) library to power [Apache Mesos](https://github.com/apache/mesos), which itself has been used at massive scale (production clusters of > 80k hosts).

## User Guide

### Bazel

For an example of how to depend on eventuals via Bazel in your own project you'll need to copy the lines selected in [WORKSPACE.bazel](https://github.com/3rdparty/eventuals-tutorial/blob/main/WORKSPACE.bazel#L3-L31) from our [eventuals-tutorial](https://github.com/3rdparty/eventuals-tutorial) repository.

### Eventual's theory

Most of the time you'll use higher-level **_combinators_** for composing eventuals together. This guide will start with more basic ones and work our way up to creating your own eventuals.

You **_compose_** eventuals together using an overloaded `operator>>()`. You'll see some examples shortly. The syntax is similar to Bash "pipelines" (but instead of `|` we use `>>`) and we reuse the term pipeline for eventuals as well.

Note that we use `operator>>()` instead of `operator|()` because it provides safer expression evaluation order in C++17 and on. See [this paper](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0145r3.pdf) for more details.

Because the result type of a composed pipeline is not type-erased you'll use `auto` generously, e.g., as function return types.

You must explicitly **_start_** an eventual in order for it to run. You'll only start eventuals at the "edges" of your code, e.g., in `int main()`. Before you start an eventual you must first "terminate it" by composing with a `Terminal()`:

```cpp
auto e = AsynchronousFunction()
    >> Terminal()
          .start([](auto&& result) {
            // Eventual pipeline succeeded!
          })
          .fail([](auto&& result) {
            // Eventual pipeline failed!
          })
          .stop([](auto&& result) {
            // Eventual pipeline stopped!
          });
```

A terminated eventual can then be "built" into what we call an eventual's _continuation_ form:

```cpp
auto k = Build(std::move(e));
```

You'll often see the variable `k` to refer to an eventual in it's continuation form. Finally you can start it with:

```cpp
k.Start();
```

Note that once you start an eventual **_it must not be deallocated/destructed and can not be moved until after it has completed_**.

To integrate eventuals with `std::future` you can use the helper `Terminate()` that calls `Build()` on an eventual terminated with an implementation of `Terminal()` that integrates `std::promise` and `std::future`:

```cpp
auto e = AsynchronousFunction();

auto [future, k] = Terminate(std::move(e));

k.Start();

auto result = future.get(); // Wait for the eventual to complete.
```

You'll often see this form in tests. You can reduce the above boilerplate further using an overloaded `operator*()`:

```cpp
auto result = *AsynchronousFunction();
```

But this **_is blocking_** and shoud only be done in tests!

Ok, let's dive into some eventuals!

### `Just`

The most basic of all eventuals, `Just()` allows you to inject a value into a pipeline:

```cpp
Just("hello world");
```

### `Then`

Probably the most used of all the combinators, `Then()` continues a pipeline with the value that was asynchronously computed:

```cpp
http::Get("https://3rdparty.dev")
    >> Then([](http::Response&& response) {
        // Return an eventual that will automatically get started.
        return SomeAsynchronousFunction(response);
      });
```

You don't have to return an eventual in the callable passed to `Then()`, you can also return a synchronous value:

```cpp
http::Get("https:://3rdparty.dev")
    >> Then([](auto&& response) {
        // Return a value that will automatically get propagated.
        return response.code == 200;
      });
```

### `If`

When you need to _conditionally_ continue using two differently typed eventuals you use `If()`, i.e., an asynchronous "if" statement:

```cpp
http::Get("https:://3rdparty.dev")
    >> Then([](auto&& response) {
        // Try for the 'www' host if we don't get a 200.
        return If(response.code != 200)
            .then(http::Get("https:://www.3rdparty.dev"))
            .otherwise(Just(response));
      });
```

`If()` uses the builder pattern for specifying the "then" and "else" branch, the latter of which we call "otherwise" since "else" is a reserved keyword. Both `.then()` and `.otherwise()` expect an eventual, but if you wanted to do any extra processing you can do so with a `Then()`:

```cpp
http::Get("https:://3rdparty.dev")
    >> Then([](auto&& response) {
        // Try for the 'www' host if we don't get a 200.
        return If(response.code != 200)
            .then(http::Get("https:://www.3rdparty.dev"))
            .otherwise(Then([body = response.body]() {
              return "Received HTTP Status OK with body: " + body;
            }));
      });
```

### Errors and Error Handling

Depending on whether your writing synchronous or asynchronous code there are a few different strategies for both creating and handling errors.

#### Synchronous Errors

When working with _synchronous_ code you should return an `Expected::Of<T>` type:

```cpp
Expected::Of<std::string> GetFullName(const Person& person) {
  if (person.has_first_name() && person.has_last_name()) {
    return Expected(person.first_name() + " " + person.last_name());
  } else {
    return Unexpected(InvalidPerson("name incomplete"));
  }
}
```

Note that while `Unexpected()` is necessary to return an error, the use of `Expected()` is not strictly necessary but makes for more explicit code.

If you have a lambda that returns both `Expected()` and `Unexpected()` you'll need to explicitly specify `Expected::Of<T>` so the compiler knows how to convert an `Unexpected()` into the proper type (because `Unexpected()` doesn't know what `T` is and rather than having you specify `T` for every call to `Unexpected()` which can be numerous in a function body you instead can specify `Expected::Of<T>` once in the return type):

```cpp
auto get_full_name = [](const Person& person) -> Expected::Of<std::string> {
  if (person.has_first_name() && person.has_last_name()) {
    return Expected(person.first_name() + " " + person.last_name());
  } else if (!person.has_first_name()) {
    return Unexpected(InvalidPerson("missing first name"));
  } else {
    CHECK(!person.has_last_name());
    return Unexpected(InvalidPerson("missing last name"));
  }
};
```

An `Expected:Of<T>` _composes_ with other eventuals exactly as though it is an eventual itself. You can return an `Expected::Of<T>` where you might return an eventual:

```cpp
ReadPersonFromFile(file)
    >> Then([](Person&& person) {
        return GetFullName(person);
      })
    >> Then([](std::string&& full_name) {
        ...
      });
```

Or you can compose an eventual with `>>` which can be useful in cases where want the error to propagate:

```cpp
ReadPersonFromFile(file)
    >> Then(Let([](auto& person) {
        return GetFullName(person)
            >> Then([&](auto&& full_name) {
                 if (person.has_suffix) {
                   return full_name + " " + person.suffix();
                 } else {
                   return full_name;
                 }
               });
      }));
```

#### Asynchronous Errors

Working with _asynchronous_ code is a little complicated because there might be multiple eventuals returned that propagate the same type of value (e.g., a `Just(T())` and an `Eventual<T>`), but they themselves are _different_ types. In synchronous code you'll only ever be returning `T()` or `Expected(T())` and the compiler doesn't take much to be happy. To solve this problem asynchronous code can use `If()` to _conditionally_ return differently typed continuations. And an error can be raised with `Raise()`:

```cpp
auto GetBody(const std::string& uri) {
  return http::Get(uri)
      >> Then([](auto&& response) {
           return If(response.code == 200)
               .then(Just(response.body))
               .otherwise(Raise("HTTP GET failed w/ code " + std::to_string(response.code)));
         });
}
```

But as we already saw `If()` is not _only_ useful for errors; it can also be used anytime you have conditional continuations:

```cpp
auto GetOrRedirect(const std::string& uri, const std::string& redirect_uri) {
  return http::Get(uri)
      >> Then([redirect_uri](auto&& response) {
           // Redirect if 'Service Unavailable'.
           return If(response.code == 503)
               .then(http::get(redirect_uri))
               .otherwise(Just(response));
         });
}
```

### Synchronization

Synchronization is just as necessary with asynchronous code as with synchronous code, except you can't use existing abstractions like `std::mutex` because these are blocking! Instead, you need to use asynchronous aware replacements such as `Lock`:

```cpp
Lock lock;

AsynchronousFunction()
    >> Acquire(&lock)
    >> Then([](auto&& result) {
        // Protected by 'lock' ...
      })
    >> Release(&lock);
```

This is often used when capturing `this` to use as part of some asynchronous computation. To simplify this common pattern you can extend your classes with `Synchronizable` and then use `Synchronized()`:

```cpp
class MyClass : public Synchronizable {
 public:
  auto MyMethod() {
    return Synchronized(
        Then([](auto&& result) {
          // Protected by 'Synchronizable::lock()' ...
        }));
  }
};
```

#### `ConditionVariable`

Sometimes you need to "wait" for a specific condition to become true while holding on to the lock, e.g., you need a condition variable. You can do that with a `ConditionVariable`:

```cpp
class SomeAggregateSystem : public Synchronizable {
 public:
  SomeAggregateSystem()
    : initialization_(&lock()) {}

  auto MyMethod() {
    return Synchronized(
        // Need to wait until we've completed initialization.
        initalization_.Wait([]() {
          return cooling_subsystem_initialized_
              && safety_subsystem_initialized_;
        })
        >> Then([](auto&& result) {
            // ...
          }));
  }

  auto InitializeCoolingSubsystem() {
    return CoolingSubsystemInitialization()
        >> Synchronized(
               Then([this]() {
                 cooling_subsystem_initialized_ = true;
                 initialization_.Notify();
               }));
  }

  auto InitializeSafetySubsystem() { ... }

 private:
  ConditionVariable initialization_;
};
```

If you just want to wait for a single call to `Notify()` you can invoke `Wait()` with no arguments.

For a good example of `Synchronized()` and `ConditionVariable` see `eventuals/pipe.h`.

### `Task`

You can use a `Task` to type-erase your continuation or pipeline. Currently this performs dynamic heap allocation but in the future we'll likely provide a `SizedTask` version that lets you specify the size such that you can type-erase without requiring dynamic heap allocation. Note however, that `Task` requires a callable in order to delay the dynamic heap allocation until the task is started so that the current scheduler has a chance of optimizing the allocation based on the current execution resource being used (e.g., allocating from the local NUMA node for the current thread).

```cpp
Task::Of<int> task = []() { return Asynchronous(); };
```

You can compose a `Task::Of` just like any other eventual as well:

```cpp
auto e = Task::Of<int>([]() { return Asynchronous(); })
    >> Then([](int i) {
           return stringify(i);
         });
```

A `Task::Of` needs to be terminated just like any other eventual unless the callable passed to `Task` is terminted. In tests you can use `*` just like you can with any other eventual, but remember this **_blocks_** the current thread!

### Abstract Classes and Virtual Methods

You can create abstract classes that allow derived classes to either provide a synchronous or asynchronous implementation using `Task::Of`. Consider the following class:

```cpp
class Base {
 public:
  virtual Task::Of<std::string> Method() = 0;
};
```

Now a derived class that has a _synchronous_ implementation:

```cpp
class DerivedSynchronous : public Base {
 public:
  Task::Of<std::string> Method() override {
    if (SomeCondition()) {
      return Task::Success("success");
    } else {
      return Task::Failure("failure");
    }
  }
};
```

This is similar to `Expected()` and `Unexpected()`, but named explicitly to avoid confusion.

And a derived class that has an _asynchronous_ implementation:

```cpp
class DerivedAsynchronous : public Base {
 public:
  Task::Of<std::string> Method() override {
    return []() {
      return AsynchronousFunction()
          >> Then([](bool condition) -> Expected::Of<std::string> {
               if (condition) {
                 return Expected("success");
               } else {
                 return Unexpected("failure");
               }
             });
    };
  }
};
```

### `Eventual`

When you need more control over the asynchronous computation you use `Eventual`. Here's a simple one:

```cpp
Eventual<std::string>([](auto& k) {
  k.Start("hello world");
});
```

This will _eventually_ start `k`, the next eventual in the pipeline (in continuation form), with the value `"hello world"`.

You can more explicitly create an eventual by following the "builder pattern":

```cpp
Eventual()
    .start([](auto& k) {
      k.Start("hello world");
    });
```

This is useful when you also want to specify what to do when either a failure or stop is being propgated from the previous eventual in the pipeline:

```cpp
Eventual<std::string>()
    .start([](auto& k) {
      k.Start("hello world");
    })
    .fail([](auto& k, auto&&... errors) {
      // Handle raised errors.
    })
    .stop([](auto& k) {
      // Handle stopped computation.
    })
```

Each callback takes the continuation `k` and continues the pipeline as it sees fit (i.e., the `fail` callback can "recover" and call `k.Start(...)` if it wants).

You can also call `.context()` which allows you to "capture" data that you can use in each callback:

```cpp
Eventual<std::string>()
    .context("hello world")
    .start([](auto& data, auto& k) {
      k.Start(data);
    })
    .fail([](auto& data, auto& k, auto&&... errors) {
      // Handle raised errors.
    })
    .stop([](auto& data, auto& k) {
      // Handle stopped computation.
    });
```

In many cases you can simply capture what you need in an individual callback, but sometimes you may need to use data across callbacks.

#### Interrupting an Eventual

Sometimes after you've started an eventual you'll want to cancel or stop it. You can do so by interrupting it. By default an eventual is not interruptible, but you can make it interruptible by doing the following:

```cpp
Eventual<std::string>()
    .interruptible()
    .start([](auto& k, Interrupt::Handler& handler) {
      handler.Install([&k]() {
        // Handle interruption ... in this case by
        // propagating 'Stop()' to the next eventual.
        k.Stop();
      });
      k.Start("hello world");
    });
```

The above example isn't very interesting because the start callable isn't actually asynchronous, but if it was then you can manage whether or not you call `k.Start(...)` or `k.Stop()` or `k.Fail(...)` depending on when you get an interrupt and what your semantics are for handling interrupts.

You can register an interrupt with an eventual (pipeline) and trigger the interrupt like so:

```cpp
auto [future, k] = Terminate(
    Eventual<std::string>()
        .interruptible()
        .start([](auto& k, Interrupt::Handler& handler) {
          handler.Install([&k]() {
            k.Stop();
          });
          // Imitate a really long asynchronous computation by just never
          // starting the continuation 'k' ...
        }));

Interrupt interrupt;

k.Register(interrupt);

k.Start();

interrupt.Trigger();

future.get(); // Will throw an exception that the eventual was interrupted!
```

Note we chose the more broad "interrupt" instead of "cancel" as there may be many possible reasons for "interrupting" an eventual beyond just for "cancellation". When creating a general abstraction, however, error on the side of assuming that interrupt means cancel.

### `Stream`, `Repeat`, and `Loop`

You can use `Stream` to "return" multiple values asynchronously. Instead of using `succeed(), `fail()`, and `stop()`as we've already seen, "streams" use`emit()`and`ended()` which emit a value on the stream and signify that there are no more values, respectively.

You "convert" a stream back into an eventual with a `Loop` and use `next()`, `done()` to request the next value on the stream or tell the stream that you don't want any more values, respectively.

By default streams are not buffered so as to be able to provide explicit flow control and back pressure. Here's a simple stream and loop:

```cpp
Stream<int>()
  .context(5)
  .next([](auto& count, auto& k) {
    if (count-- > 0) {
      emit(k, count);
    } else {
      ended(k);
    }
  })
  >> Loop<int>()
     .context(0)
     .body([](auto& sum, auto& stream, auto&& value) {
       sum += value;
       next(stream);
     })
     .ended([](auto& sum, auto& k) {
       succeed(k, sum);
     });
```

You can construct a stream out of repeated asynchronous computations using `Repeat`:

```cpp
Repeat([]() { return Asynchronous(); });
```

`Repeat()` acts just like `Stream()` where you can continue it with a `Loop()` that calls `next()` and `done()` for you.

### `Map` and `Reduce`

Often times you'll want to perform some transformations on your stream. You can do that with `Map()`. Here's an example of doing a "map reduce":

```cpp
Iterate({1, 2, 3, 4, 5})
    >> Map([](int i) {
        return i + 1;
      })
    >> Reduce(
        /* sum = */ 0,
        [](auto& sum) {
          return Then([&](auto&& value) {
            sum += value;
            return true;
          });
        });
```

### Infinite `Loop`

Sometimes you'll have an infinite stream. You can loop over it infinitely by using `Loop()`:

```cpp
SomeInfiniteStream()
  >> Map([](auto&& i) { return Foo(i); })
  >> Loop(); // Infinitely loop.
```

### `http`

The `http` namespace provides HTTP client and (work in progress) server implementations.

An HTTP `GET`:

```cpp
http::Get("http://example.com") // Use 'https://' for TLS/SSL.
    >> Then([](http::Response&& response) {
        // ...
      });
```

An HTTP `POST`:

```cpp
http::Post(
    "https://jsonplaceholder.typicode.com/posts",
    {{"first", "emily"}, {"last", "schneider"}})
    >> Then([](auto&& response) {
        // ...
      });
```

For more control over the HTTP request create an `http::Client`. For example, if you don't want to verify peers when using HTTPS you can do:

```cpp
http::Client client = http::Client::Builder()
                          .verify_peer(false)
                          .Build();

client.Post(
    "https://jsonplaceholder.typicode.com/posts",
    {{"first", "emily"}, {"last", "schneider"}})
    >> Then([](auto&& response) {
        // ...
      });
```

Or to control individual requests use an `http::Request`. For example, to add headers:

```cpp
client.Do(
    http::Request::Builder()
        .uri("https://3rdparty.dev")
        .method(http::GET)
        .header("key", "value")
        .header("another", "example")
        .Build())
    >> Then([](auto&& response) {
        // ...
      });
```

Anything added to an `http::Request` overrides an `http::Client`:

```cpp
client.Do(
    http::Request::Builder()
        .uri("https://3rdparty.dev")
        .method(http::GET)
        .verify_peer(true) // Overrides client!
        .Build())
    >> Then([](auto&& response) {
        // ...
      });
```

#### TLS/SSL Certificate Verification

As you already saw above, you can skip verification by doing `verify_peer(false)` when building an `http::Client` or `http::Request`.

You can also provide a CA certificate that can verify the peer:

```cpp
// Read a PEM encoded certificate from a file.
std::filesystem::path path = "/path/to/certificate";

Expected::Of<x509::Certificate> certificate = pem::ReadCertificate(path);

CHECK(certificate); // Handle as appropriate.

http::Client client = http::Client::Builder()
                          .certificate(*certificate)
                          .Build();

client.Get("https://3rdparty.dev")
    >> Then([](auto&& response) {
        // ...
      });
```

Alternatively you can add the certificate per request:

```cpp
// Read a PEM encoded certificate from a file.
std::filesystem::path path = "/path/to/certificate";

Expected::Of<x509::Certificate> certificate = pem::ReadCertificate(path);

http::Client client = http::Client::Builder().Build();

client.Do(
    http::Request::Builder()
        .uri("https://3rdparty.dev")
        .method(http::GET)
        .certificate(*certificate)
        .Build())
    >> Then([](auto&& response) {
        // ...
      });
```

#### RSA, X.509, and PEM

To create an RSA keypair:

```cpp
Expected::Of<rsa::Key> key = rsa::Key::Builder().Build();
```

To create an X.509 certificate for some IP `address` use the RSA `key` created above as the certificate subject and for signing the certificate:

```cpp
Expected::Of<x509::Certificate> certificate =
    x509::Certificate::Builder()
        .subject_key(rsa::Key(*key))
        .sign_key(rsa::Key(*key))
        .ip(address)
        .Build();
```

To encode `key` or `certificate` in PEM format (which can then be written to a file):

```cpp
Expected::Of<std::string> pem_key = pem::Encode(*key);

Expected::Of<std::string> pem_certificate = pem::Encode(*certificate);
```

To get an `x509::Certificate` from a PEM encoded file:

```cpp
// Read a PEM encoded certificate from a file.
std::filesystem::path path = "/path/to/certificate";

Expected::Of<x509::Certificate> certificate = pem::ReadCertificate(path);
```

### `grpc`

An asynchronous interface for gRPC based on callbacks. While gRPC already provides an asynchronous [interface](https://grpc.io/docs/languages/cpp/async), it is quite low-level. gRPC has an experimental (as of 2020/07/10, still [in progress](https://github.com/grpc/grpc/projects/12)) higher-level asynchronous interface based on a [reactor pattern](https://grpc.github.io/grpc/cpp/classgrpc__impl_1_1_server_bidi_reactor.html), it is still based on inheritance and overriding virtual functions to receive "callbacks" and as such is hard to compose with other asynchronous code. Moreover, it's still relatively low-level, e.g., it only permits a single write at a time, requiring users to buffer writes on their own.

`eventuals::grpc` is intended to be a higher-level inteface while still being asynchronous to make composing asynchronous code easy. It deliberately tries to mimic `grpc` naming where appropriate to simplify understanding across interfaces.

Please see [Known Limitations](#known-limitations) and [Suggested Improvements](#suggested-improvements) below for more insight into the limits of the inteface.

### Examples

Examples can be found [here](https://github.com/3rdparty/eventuals-grpc-examples) (but these need to be updated as of 2022/03/05).

They have been put in a separate repository to make it easier to clone that repository and start building a project rather than trying to figure out what pieces of the build should be copied.

We recommend cloning the examples and building them in order to play around with the library.

#### Logging

[glog](https://github.com/google/glog) is used to perform logging. You'll need to enable glog verbose logging by setting the environment variable `GLOG_v=1` (or any value greater than 1) as well as the enironment variable `EVENTUALS_GRPC_LOG=1`. You can call `google::InitGoogleLogging(argv[0]);` in your own `main()` function to properly initialize glog.

#### Known Limitations

- Services can not (yet) be removed after they are "added" via `Server::Accept()`.

- One of the key design requirements was being able to add a "service" dynamically, i.e., after the server has started, by calling `Server::Accept()`. This doesn't play nicely with some built-in components of gRPC, such as server reflection (see below). In the short-term we'd like to support adding services **before** the server starts that under the covers use `RegisterService()` so that those services can benefit from any built-in components of gRPC.

- Server Reflection via the (gRPC Server Reflection Protocol)[https://github.com/grpc/grpc/blob/master/doc/server-reflection.md] **_requires_** that all services are registered before the server starts. Because `eventuals::grpc::Server` is designed to allow services to be added dynamically via invoking `Server::Accept()` at any point during runtime, the reflection server started via `grpc::reflection::InitProtoReflectionServerBuilderPlugin()` will not know about any of the services. One possibility is to build a new implementation of the reflection server that works with dynamic addition/removal of services. A short-term possibility is to only support server reflection for services added before the server starts.

- No check is performed that all methods of a particular service are added via `Server::Accept()`. In practice, this probably won't be an issue as a `grpc::UNIMPLEMENTED` will get returned which is similar to how a lot of services get implemented incrementally (i.e., they implement one method at a time and return a `grpc::UNIMPLEMENTED` until they get to said method).

### Scheduling and Memory Allocation

_... to be completed ..._

### Bazel

You can easily incorporate eventuals into your own build if you're using Bazel.

Copy `bazel/repos.bzl` into the directory `3rdparty/eventuals` of your
own project/workspace and add an empty `BUILD.bazel` into that
directory as well.

Now you can add the following to your `WORKSPACE` (or `WORKSPACE.bazel`):

```
load("//3rdparty/eventuals:repos.bzl", eventuals_repos = "repos")
eventuals_repos()

load("@com_github_3rdparty_eventuals//bazel:deps.bzl", eventuals_deps="deps")
eventuals_deps()
```

You can then depend on `@eventuals//eventuals` in your Bazel targets.

## Contributing

### Building/Testing

Currently we only support [Bazel](https://bazel.build) and expect/use C++17 (some work could likely make this C++14).

You can build the library with:

```sh
$ bazel build :eventuals
...
```

You can build and run the tests with:

```sh
$ bazel test test:eventuals
...
```

### Visual Studio Code and Bazel Set Up

<details><summary>macOS</summary>
<p>

1. Download and install [Visual Studio Code](https://code.visualstudio.com/download) (VS Code).
2. Run VS Code and install the necessary extensions:
   1. [Bazel plugin](https://marketplace.visualstudio.com/items?itemName=BazelBuild.vscode-bazel). This extension provides support for Bazel in Visual Studio Code.
   2. [C/C++ plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools). The C/C++ extension adds language support for C/C++ to Visual Studio Code, including features such as IntelliSense and debugging.
   3. [Clang-format plugin](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format). This extension allows you to comply with the clang format for your code. Read the plugin overview for configuration.
   4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build). Possible instructions for doing so using Homebrew:

   1. Check the presence of Bazel using the following command in your terminal:

   ```
   $ bazel --version
   ```

   2. If you have no Bazel - install it using [Homebrew](https://brew.sh).

   ```
   3. Install the Bazel package via Homebrew as follows:
   ```

   $ brew install bazel

   ```
   4. Upgrade to a newer version of Bazel using the following command (if needed):
   ```

   $ brew upgrade bazel

   ```

   ```

4. Clone [eventuals](https://github.com/3rdparty/eventuals).
5. Open the eventuals folder via VS Code.
6. Check the checkboxes about "Trust the authors".
7. VS Code -> Terminal -> New Terminal

</p>
</details>

<details><summary>Linux</summary>
<p>

1. Download and install [Visual Studio Code](https://code.visualstudio.com/download) (VS Code).
2. Run VS Code and install the necessary extensions:
   1. [Bazel plugin](https://marketplace.visualstudio.com/items?itemName=BazelBuild.vscode-bazel). This extension provides support for Bazel in Visual Studio Code.
   2. [C/C++ plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools). The C/C++ extension adds language support for C/C++ to Visual Studio Code, including features such as IntelliSense and debugging.
   3. [Clang-format plugin](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format). This extension allows you to comply with the clang format for your code. Read the plugin overview for configuration.
   4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build).
4. Install the latest version of the compiler [LLVM](https://llvm.org) ([LLVM Download Page](https://releases.llvm.org/download.html)).
5. Install [Git](https://git-scm.com/downloads).
6. Clone [eventuals](https://github.com/3rdparty/eventuals).
7. Open the eventuals folder via VS Code.
8. Check the checkboxes about "Trust the authors".
9. VS Code -> Terminal -> New Terminal

</p>
</details>

<details><summary>Windows</summary>
<p>

1. Download and install [Visual Studio Code](https://code.visualstudio.com/download) (VS Code).
2. Run VS Code and install the necessary extensions:
   1. [Bazel plugin](https://marketplace.visualstudio.com/items?itemName=BazelBuild.vscode-bazel). This extension provides support for Bazel in Visual Studio Code.
   2. [C/C++ plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools). The C/C++ extension adds language support for C/C++ to Visual Studio Code, including features such as IntelliSense and debugging.
   3. [Clang-format plugin](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format). This extension allows you to comply with the clang format for your code. Read the plugin overview for configuration.
      Possible instuctions for how you can use Visual Studio's `clang-format`: 1. Create a folder `.vscode`in your project folder. 2. Create a file `settings.json` in the folder `.vscode` 3. Add the data to the file (check the path to your `clang-format.exe`):
      `{ "clang-format.style": "Google", "clang-format.executable": "C:/Program Files (x86)/Microsoft Visual Studio/2019/ Community/VC/Tools/Llvm/x64/bin/clang-format.exe", "editor.formatOnSave": true }`
   4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build). Detailed installation instructions for Windows can be found here: [Installing Bazel on Windows](https://docs.bazel.build/versions/4.1.0/install-windows.html). This is an important step. You must follow all the instructions, otherwise you will get various errors at the compilation stage.
4. Install the latest version of the compiler [LLVM](https://llvm.org) ([LLVM Download Page](https://releases.llvm.org/download.html)).
5. Install [Git](https://git-scm.com/downloads).
6. Restart your PC. ;-)
7. Clone [eventuals](https://github.com/3rdparty/eventuals).
8. Open the eventuals folder via VS Code.
9. Check the checkboxes about "Trust the authors".
10. VS Code -> Terminal -> New Terminal

</p>
</details>

### Code Style

The eventuals library maintains a code style that's enforced by a GitHub [workflow](https://github.com/3rdparty/eventuals/blob/main/.github/workflows/check_code_style.yml). You can also install a `git` [`pre-commit`](https://github.com/3rdparty/dev-tools/blob/main/pre-commit) [hook](https://git-scm.com/book/en/v2/Customizing-Git-Git-Hooks) to check the code style locally before sending a pull request: see the [dev-tools README](https://github.com/3rdparty/dev-tools/tree/main#readme) for instructions.

### Contributor Guide

#### Builder Pattern

There are numerous places where we use the builder pattern. Unlike most builders you've probably worked with we also check for _required_ fields at compile-time by creating our builders in particular ways. There are helpers in `eventuals/builder.h` which simplify creating your own builders. Here we walk through an example of creating a builder for `http::Request`.

Starting with the `http::Request` object:

```cpp
namespace http {

class Request {};

} // namespace http
```

Declare a "builder" method which we'll implement later:

```cpp
class Request {
 public:
  static auto Builder();
};
```

And declare a "builder" type which we'll implement next:

```cpp
class Request {
 public:
  static auto Builder();

 private:
  template <bool, bool>
  struct _Builder;
};
```

What are those `bool` template parameters? Defining `_Builder` should help explain:

```cpp
template <bool has_method_, bool has_uri_>
class Request::_Builder : public builder::Builder {
 private:
  builder::Field<Method, has_method_> method_;
  builder::Field<std::string, has_uri_> uri_;
};
```

Each `bool` template parameter represents whether or not this builder has that field set or not. Each field is a `builder::Field` which you can think of as a compile-time version of `std::optional`. A `builder::Field` requires that you specify as the first parameter the type of the field (e.g., `std::string`) and the second field represents whether or not it has been set. Don't worry, you can't by accident set the second parameter to `true` without also actually setting a value!

Note that we inherit from `builder::Builder` which provides a `Construct()` method we'll use below.

Now let's create a default constructor for creating the initial builder as well as a constructor that takes all our fields. Note that we'll also make our outer most class, `http::Request` in this case be a `friend` so it can call our default constructor.

```cpp
template <bool has_method_, bool has_uri_>
class Request::_Builder : public builder::Builder {
 private:
  friend class Request;

  _Builder() {}

  _Builder(
      builder::Field<Method, has_method_> method,
      builder::Field<std::string, has_uri_> uri)
    : method_(std::move(method)),
      uri_(std::move(uri)) {}

  builder::Field<Method, has_method_> method_;
  builder::Field<std::string, has_uri_> uri_;
};
```

And now we'll add our field "setters", starting with the setter for `method`:

```cpp
template <bool has_method_, bool has_uri_>
class Request::_Builder : public builder::Builder {
 public:
  auto method(Method method) && {
    static_assert(!has_method_, "Duplicate 'method'");
    return Construct<_Builder>(
        method_.Set(method),
        std::move(uri));
  }

 private:
  friend class Request;

  _Builder() {}

  _Builder(
      builder::Field<Method, has_method_> method,
      builder::Field<std::string, has_uri_> uri)
    : method_(std::move(method)),
      uri_(std::move(uri)) {}

  builder::Field<Method, has_method_> method_;
  builder::Field<std::string, has_uri_> uri_;
};
```

Let's zoom in and break down what's happening here:

```cpp
  auto method(Method method) && {
    static_assert(!has_method_, "Duplicate 'method'");
    return Construct<_Builder>(
        method_.Set(method),
        std::move(uri));
  }
```

First, if a field should only be set once, the setter can do a `static_assert()` to check if the field has already been set! Second, every setter creates a fresh builder with all of the fields from the previous builder except the field being set which gets set by doing `field_.Set(...)`. The `Construct()` method takes all the fields and the type of the builder and creates the fresh builder for us by calling that constructor that we specified earlier. Since we had made that constructor `private` we need to make our base class `builder::Builder` be a `friend` so it can call our constructor:

```cpp
template <bool has_method_, bool has_uri_>
class Request::_Builder : public builder::Builder {
 public:
  ...

 private:
  friend class builder::Builder;

  ...
};
```

Note that every "setter" method requires the receiver (i.e., `this`)
to be an rvalue reference (the `&&` after the arguments in the function signature) so that we can efficiently move the builder values from one builder to the next. This isn't strictly required, but this pattern helps catch users from thinking that your builder is "mutable" when they don't make sure that they're calling a setter with an rvalue receiver. Note that most of the time this won't be a problem because users will just chain calls to setters.

After adding all the setters for the fields we'll need `Build()`:

```cpp
template <bool has_method_, bool has_uri_>
class Request::_Builder : public builder::Builder {
 public:
  ...

  Request Build() && {
    static_assert(has_method_, "Missing 'method'");
    static_assert(has_uri_, "Missing 'uri'");

    if (method_.value() == Method::GET) {
       ...
    }
  }

  ...
};

```

Inside `Build()` we can check to make sure that all of the _required_ fields have been set. You can use each `builder::Field` fields similar to `std::optional` by calling the `value()` family of methods or using `operator->()`. Again, if code that calls `value()` compiles then there must be a value! That is to say, the `static_assert()` provide for better error messages for users but they aren't what's protecting you from trying to get a value that doesn't exist.

To finish this off we'll need to implement `http::Request::Builder()`:

```cpp
inline auto http::Request::Builder() {
  return _Builder<false, false>();
}
```

And that's it!

Let's add another field, a `timeout` so that we can demonstrate fields with default values (but to be clear `http::Request` does not have a default value for `timeout`). We start by adding a `bool` template parameter everywhere to represent `has_timeout`. Instead of `builder::Field` we'll use `builder::FieldWithDefault` and provide a default value as part of the declaration:

```cpp
template <bool has_method_, bool has_uri_, bool has_timeout_>
class Request::_Builder : public builder::Builder {
  ...

 private:
  ...

  builder::FieldWithDefault<std::chrono::nanoseconds, has_timeout_>
      timeout_ = std::chrono::seconds(10);
};
```

We'll need to update our constructor to take the new field:

```cpp
template <bool has_method_, bool has_uri_, bool has_timeout_>
class Request::_Builder : public builder::Builder {
 public:
  ...

 private:
  ...

  _Builder(
      builder::Field<Method, has_method_> method,
      builder::Field<std::string, has_uri_> uri,
      builder::FieldWithDefault<std::chrono::nanoseconds, has_timeout_> timeout)
    : method_(std::move(method)),
      uri_(std::move(uri)),
      timeout_(std::move(timeout)) {}

  ...
};
```

And add a setter:

```cpp
  auto timeout(std::chrono::nanoseconds timeout) && {
    static_assert(!has_timeout_, "Duplicate 'timeout'");
    return Construct<_Builder>(
        std::move(method_),
        std::move(uri_),
        timeout_.Set(timeout));
  }
```

That's it! Unlike `builder::Field` where only `value()` exists after the field has been set, `builder::FieldWithDefault` always has a `value()` and depending on whether or not the field was set it either returns the set field or the default.
