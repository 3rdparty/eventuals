[![macOS](https://github.com/3rdparty/stout-eventuals/workflows/macOS/badge.svg?branch=main)](https://github.com/3rdparty/stout-eventuals/actions/workflows/macos.yml)
[![Ubuntu](https://github.com/3rdparty/stout-eventuals/workflows/Ubuntu/badge.svg?branch=main)](https://github.com/3rdparty/stout-eventuals/actions/workflows/ubuntu.yml)
[![Windows](https://github.com/3rdparty/stout-eventuals/workflows/Windows/badge.svg?branch=main)](https://github.com/3rdparty/stout-eventuals/actions/workflows/windows.yml)

# Eventuals

A C++ library for composing asynchronous ***continuations*** without locking or requiring dynamic heap allocations.

**Callbacks** are the most common approach to *continue* an asynchronous computation, but they are hard to compose, don't (often) support cancellation, and are generally tricky to reason about.

**Futures/Promises** are an approach that does support composition and cancellation, but many implementations have poor performance due to locking overhead and dynamic heap allocations, and because the evaluation model of most futures/promises libraries is **eager** an expression isn't referentially transparent.

**Eventuals** are an approach that, much like futures/promises, support composition and cancellation, however, are **lazy**. That is, an eventual has to be explicitly *started*.

Another key difference from futures/promises is that an eventual's continuation is **not** type-erased and can be directly used, saved, etc by the programmer. This allows the compiler to perform significant optimizations that are difficult to do with other lazy approaches that perform type-erasure. The tradeoff is that more code needs to be written in headers, which may lead to longer compile times. You can, however, mitgate long compile times by using a type-erasing `Task` type (more on this later).

The library provides numerous abstractions that simplify asynchronous continuations, for example a `Stream` for performing asynchronous streaming. Each of these abstractions follow a "builder" pattern for constructing them, see [Usage](#usage) below for more details.

This library was inspired from experience building and using the [libprocess](https://github.com/3rdparty/libprocess) library to power [Apache Mesos](https://github.com/apache/mesos), which itself has been used at massive scale (production clusters of > 80k hosts).

### Contact

Please reach out to `stout@3rdparty.dev` for any questions or if you're looking for support.

## Building/Testing

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
4. Clone [stout-eventuals](https://github.com/3rdparty/stout-eventuals).
5. Open the stout-eventuals folder via VS Code.
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
6. Clone [stout-eventuals](https://github.com/3rdparty/stout-eventuals).
7. Open the stout-eventuals folder via VS Code.
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
    Possible instuctions for how you can use Visual Studio's `clang-format`:
        1. Create a folder `.vscode`in your project folder.
        2. Create a file `settings.json` in the folder `.vscode`
        3. Add the data to the file (check the path to your `clang-format.exe`):
        ```
        {
        "clang-format.style": "Google",
        "clang-format.executable": "C:/Program Files (x86)/Microsoft Visual Studio/2019/
        Community/VC/Tools/Llvm/x64/bin/clang-format.exe",
        "editor.formatOnSave": true
        }
        ```
    4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build). Detailed installation instructions for Windows can be found here: [Installing Bazel on Windows](https://docs.bazel.build/versions/4.1.0/install-windows.html). This is an important step. You must follow all the instructions, otherwise you will get various errors at the compilation stage.
4. Install the latest version of the compiler [LLVM](https://llvm.org) ([LLVM Download Page](https://releases.llvm.org/download.html)).
5. Install [Git](https://git-scm.com/downloads).
6. Restart your PC. ;-)
7. Clone [stout-eventuals](https://github.com/3rdparty/stout-eventuals).
8. Open the stout-eventuals folder via VS Code.
9. Check the checkboxes about "Trust the authors".
10. VS Code -> Terminal -> New Terminal

</p>
</details>

## Usage

### Bazel

Add the following to your `WORKSPACE` (or `WORKSPACE.bazel`):

```
git_repository(
  name = "stout-eventuals",
  remote = "https://github.com/3rdparty/stout-eventuals",
  commit = "579b62a16da74a4e197c96b39b3ecca39c00452f",
  shallow_since = "1624126303 -0700",
)
```

You can then depend on `@stout-eventuals//:eventuals` in your Bazel targets.

### Tutorial

#### "Building" an Eventual

An `Eventual` provides **explicit** control of performing a simple asynchronous computation. First you "build" an eventual by specifying the type of the value that it will eventually compute and override the `.start()` callback for performing the computation:

```cpp
auto e = Eventual<Result>()
  .start([](auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
```

In addition to `.start()` you can also override `.fail()` and `.stop()` callbacks:

```cpp
auto e = Eventual<Result>()
  .start([](auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
  .fail([](auto& k, auto&&... errors) {
    // Handle raised errors.
  })
  .stop([](auto& k) {
    // Handle stopped computation.
  })
```

Each callback takes the ***continuation*** `k` which you can use to either `succeed(k, result)` the computation, `fail(k, error)` the computation, or `stop(k)` the computation.

You can also override the `.context()` callback which allows you to "capture" data that you can use in each other callback:

```cpp
auto e = Eventual<Result>()
  .context(SomeData())
  .start([](auto& data, auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
  .fail([](auto& data, auto& k, auto&&... errors) {
    // Handle raised errors.
  })
  .stop([](auto& data, auto& k) {
    // Handle stopped computation.
  })
```

In many cases you can simply capture what you need in an individual callback, but sometimes you may want to use some data across callbacks.

#### Running and Composing an Eventual

You can use the `*` operator to *run* the asynchronous computation:

```cpp
auto result = *e;
```

But this ***blocks*** the current thread and except in tests is not (likely) what you want to do. Instead, to use the eventually computed value you want to create a "pipeline" of eventuals using the `|` operator:

```cpp
auto e2 = e
 | Eventual<std::string>()
     .start([](auto& k, auto&& result) {
       // Use result, either synchronously or asynchronously.
       succeed(k, result.ToString());
     });
```

And when you're all done add a `Terminal`:

```cpp
auto e2 = e
 | Eventual<T>()
     .start([](auto& k, auto&& result) {
       // Use result, either synchronously or asynchronously.
       succeed(k, use(result));
     })
 | Terminal()
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

Then you can explicitly *start* the eventual:

```cpp
start(e);
```

Note that once you start an eventual ***it must exist and can not be moved until after it has terminated***.

#### Interrupting an Eventual

Sometimes after you've started an eventual you'll want to cancel or stop it. You can do so by interrupting it. By default an eventual is not interruptible, but you can override the `.interrupt()` handler if you want to make your eventual interruptible:

```cpp
auto e = Eventual<Result>()
  .start([](auto& k) {
    // Perform some asynchronous computation ...
  })
  .interrupt([](auto& k) {
    // Try and interrupt the asynchronous computation.
  });
```

Then you can register an interrupt and trigger the interrupt like so:

```cpp
auto e = Eventual<Result>()
  .start([](auto& k) {
    // Perform some asynchronous computation ...
  })
  .interrupt([](auto& k) {
    // Try and interrupt the asynchronous computation.
  })
  | Terminal()
     .start([](auto&& result) {
       // Eventual pipeline succeeded!
     })
     .fail([](auto&& result) {
       // Eventual pipeline failed!
     })
     .stop([](auto&& result) {
       // Eventual pipeline stopped!
     });

Interrupt interrupt;

e.Register(interrupt);

start(e);

interrupt.Trigger();
```

Note that there may be other reasons why you want to "interrupt" an eventual, so rather than call this functionality explicitly "cancel", we chose the more broad "interrupt". When creating a general abstraction, however, error on the side of assuming that interrupt means cancel.

### `Then`

When your continuation is ***asynchronous*** (i.e., you need to create another eventual based on the result of an eventual) but you *don't* need the explicit control that you have with an `Eventual` you can use `Then`:

```cpp
auto e = Eventual<T>()
  .start([](auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
  | Then([](auto&& result) {
    // Return an eventual that will automatically get started.
    return SomeAsynchronousComputation(result);
  };
```

Sometimes your continuation is synchronous, i.e., it won't block the current thread. While you can still use an `Eventual` you can also simplify by using a `Then`:

```cpp
auto e = Eventual<T>()
  .start([](auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
  | Then([](auto&& result) {
    // Return some value ***synchronously**.
    return stringify(result);
  });
```

In many cases you can be even more implicit and just use a C++ lambda directly too:

```cpp
auto e = Eventual<T>()
  .start([](auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
  | [](auto&& result) {
    // Return some value ***synchronously**.
    return stringify(result);
  };
```

### `Just`

You can inject a value into an eventual pipeline using `Just`. This can be useful when you don't care about the result of another eventual as well as with [`Conditional`](#conditional).

```cpp
auto e = Eventual<T>()
  .start([](auto& k) {
    auto thread = std::thread(
        [&k]() mutable {
          // Perform some asynchronous computation ...
          auto result = ...;
          succeed(k, result);
        });
    thread.detach();
  })
  | Just("value");
```

### `Raise`

While `Just` is for continuing a pipeline "successfully", `Raise` can be used to trigger the failure path. Again, this becomes very useful with constructs like [`Conditional`](#conditional) amongst others.

```cpp
auto e = Raise(Error());
```

### `Conditional`

Sometimes how you want to asynchronously continue depends on some value computed asynchrhonously. You can use 'Conditional' to capture this pattern, e.g., an asynchronous "if else" statement:

```cpp
auto e = Just(1)
  | Conditional(
        [](int n) {
          return n < 0;
        },
        [](int n) {
          return HandleLessThan(n);
        },
        [](int n) {
          return HandleGreaterThanOrEqual(n);
        });
```

### Synchronization

Synchronization is just as necessary with asynchronous code as with synchronous code, except you can't use existing abstractions like `std::mutex` because these are blocking! Instead, you need to use asynchronous aware versions:


```cpp
Lock lock;

auto e = Acquire(&lock)
  | Eventual<T>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              // Perform some asynchronous computation ...
              auto result = ...;
              succeed(k, result);
            });
        thread.detach();
      })
  | Release(&lock);
```

This is often used when capturing `this` to use as part of some asynchronous computation. To simplify this common pattern you can actually extend your classes with `Synchronizable` and then do:

```cpp
auto e = Synchronized(
    Eventual<T>()
      .start([this](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              // Perform some asynchronous computation using `this`.
              auto result = ...;
              succeed(k, result);
            });
        thread.detach();
      }));
```

#### `Wait`

Sometimes you need to "wait" for a specific condition to become true while holding on to the lock. You can do that using `Wait`:

```cpp
auto e = Synchronized(
    Wait<std::string>() // NOTE: need to specify `&lock` when not using `Synchronized()`.
      .condition([this](auto& k) {
        if (...) {
          auto callback = [&k]() { notify(k); };
          // Save 'callback' in order to recheck the condition.
          wait(k);
        } else {
          succeed(k, ...);
        }
      }));
```

### `Stream`, `Repeat`, and `Loop`

You can use `Stream` to "return" multiple values asynchronously. Instead of using `succeed(), `fail()`, and `stop()` as we've already seen, "streams" use `emit()` and `ended()` which emit a value on the stream and signify that there are no more values, respectively.

You "convert" a stream back into an eventual with a `Loop` and use `next()`, `done()` to request the next value on the stream or tell the stream that you don't want any more values, respectively.

By default streams are not buffered so as to be able to provide explicit flow control and back pressure. Here's a simple stream and loop:

```cpp
auto e = Stream<int>()
  .context(5)
  .next([](auto& count, auto& k) {
    if (count-- > 0) {
      emit(k, count);
    } else {
      ended(k);
    }
  })
  | Loop<int>()
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
auto e = Repeat(Asynchronous());
```

`Repeat` acts just like `Stream` where you can continue it with a `Loop` that calls `next()` and `done()`. By default a `Repeat` will repeat forever (i.e., for every call to `next()`) but you can override the default behavior during construction:


```cpp
auto e = Repeat(Then([](int n) { return Asynchronous(n); }))
  .context(5) // Only repeat 5 times.
  .next([](auto& count, auto& k) {
    if (count-- > 0) {
      repeat(k, count);
    } else {
      ended(k);
    }
  });
```

### `Map`

Often times you'll want to perform some transformations on your stream. You can do that with `Map`. Here's an example of doing a "map reduce":

```cpp
auto e = Stream<int>()
  .context(5)
  .next([](auto& count, auto& k) {
    if (count-- > 0) {
      emit(k, count);
    } else {
      ended(k);
    }
  })
  | Map(Eventual<int>()
        .start([](auto& k, auto&& i) {
          succeed(k, i + 1);
        }))
  | Loop<int>()
     .context(0)
     .body([](auto& sum, auto& stream, auto&& value) {
       sum += value;
       next(stream);
     })
     .ended([](auto& sum, auto& k) {
       succeed(k, sum);
     });
```

### Infinite `Loop`

Sometimes you'll have an infinite stream. You can loop over it infinitely by using `Loop()`:

```cpp
auto e = SomeInfiniteStream()
  | Map(Then([](auto&& i) { return Foo(i); }))
  | Loop(); // Infinitely loop.
```

### `Task`

You can use a `Task` to type-erase your continuation or pipeline. Currently this performs dynamic heap allocation but in the future we'll likely provide a `SizedTask` version that lets you specify the size such that you can type-erase without requiring dynamic heap allocation. Note however, that `Task` requires a callable/lambda in order to delay the dynamic heap allocation until the task is started so that the current scheduler has a chance of optimizing the allocation based on the current execution resource being used (e.g., allocating from the local NUMA node for the current thread).

```cpp
Task<int> task = []() { return Asynchronous(); };
```

You can compose a `Task` just like any other eventual as well:

```cpp
auto e = Task<int>([]() { return Asynchronous(); })
  | [](int i) {
    return stringify(i);
  };
```

A `Task` needs to be terminated just like any other eventual unless the callable/lambda passed to `Task` is terminted. In tests you can use `*` to add a terminal for you, but again, be careful as this ***blocks*** the current thread!

```cpp
string s = *e; // BLOCKING! Only use in tests.
```

#### Scheduling and Memory

*... to be completed ...*
