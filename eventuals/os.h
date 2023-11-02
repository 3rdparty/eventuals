#pragma once

#include <thread>

#include "glog/logging.h" // NOTE: must be included before <windows.h>.
#include "stout/bytes.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h> // For PTHREAD_STACK_MIN.
#include <pthread.h>

#include <functional>
#include <limits>

#endif

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

#ifdef __MACH__
inline size_t GetRunningCPU() {
  // NOTE: Returning incorrect value here because
  // we don't currently know a way to correctly recognize
  // on which core the current thread is running.
  return std::numeric_limits<size_t>::max();
}

inline void SetAffinity(std::thread& thread, const size_t cpu) {
  // NOTE: We can't reliably set affinity for threads in MacOS.
  return;
}
#elif _WIN32
inline size_t GetRunningCPU() {
  return GetCurrentProcessorNumber();
}

inline void SetAffinity(std::thread& thread, const size_t cpu) {
  CHECK_NE(
      SetThreadAffinityMask(
          thread.native_handle(),
          DWORD_PTR(1) << cpu),
      0);
}
#else
inline size_t GetRunningCPU() {
  return sched_getcpu();
}

inline void SetAffinity(std::thread& thread, const size_t cpu) {
  cpu_set_t cpuset = {};
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  CHECK_EQ(
      pthread_setaffinity_np(
          thread.native_handle(),
          sizeof(cpu_set_t),
          &cpuset),
      0);
}
#endif // __MACH__

////////////////////////////////////////////////////////////////////////

namespace os {

////////////////////////////////////////////////////////////////////////

#ifndef _WIN32

struct StackInfo {
  void* start = nullptr;
  void* end = nullptr;
  Bytes size = 0;
  inline Bytes StackAvailable() {
#if defined(__linux__) || defined(__MACH__)
    char local_var{};
    return Bytes(
        (&local_var)
        - ((char*) end) - sizeof(local_var));
#else
    return Bytes{0};
#endif
  }
};

////////////////////////////////////////////////////////////////////////

inline StackInfo GetStackInfo() {
  [[maybe_unused]] pthread_t self = pthread_self();
  void* stack_addr = nullptr;
  size_t size = 0;
#ifdef __linux__
  pthread_attr_t attr;

  PCHECK(pthread_getattr_np(pthread_self(), &attr) == 0)
      << "Failed to initialize the thread attributes"
         "object referred to by attr via 'pthread_getattr_np(...)' ";

  PCHECK(pthread_attr_getstack(&attr, &stack_addr, &size) == 0)
      << "Failed to get stack address via 'pthread_attr_getstack(...)'";

  PCHECK(pthread_attr_destroy(&attr) == 0)
      << "Failed to destroy thread attributes via "
         "'pthread_attr_destroy(...)'";

  // Implementation for Windows might be added in future for this architecture.
  // For x86 stack grows downward (from highest address to lowest).
  // (check_line_length skip)
  // https://eli.thegreenplace.net/2011/02/04/where-the-top-of-the-stack-is-on-x86/
  // So the stack end will be the lowest address.
  // The function `pthread_attr_getstack` on Linux returns
  // the lowest stack address. Since the stack grows downward,
  // from the highest to the lowest address -> the stack
  // address from `pthread_attr_getstack` will be the end of
  // the stack.
  // https://linux.die.net/man/3/pthread_attr_getstack
  return StackInfo{
      /* start = */ static_cast<char*>(stack_addr) + size,
      /* end = */ stack_addr,
      /* size = */ Bytes(size)};
#endif // If Linux.

#ifdef __MACH__
  // For macOS `pthread_get_stackaddr_np` can return either the base
  // or the end of the stack. On macOS 10.7, it's the end.
  // (check_line_length skip)
  // https://android.googlesource.com/platform/art/+/master/runtime/thread.cc#1281

  // If the address of a local variable is greater than the stack
  // address from `pthread_get_stacksize_np` ->
  // `pthread_get_stacksize_np` returns the end of the stack since
  // on x86 architecture the stack grows downward (from highest to
  // lowest address).
  stack_addr = pthread_get_stackaddr_np(self);
  size = pthread_get_stacksize_np(self);
  [[maybe_unused]] char local_var{};
  if (&local_var > stack_addr) {
    return StackInfo{
        /* start = */ static_cast<char*>(stack_addr) - size,
        /* end = */ stack_addr,
        /* size = */ Bytes(size)};
  } else {
    return StackInfo{
        /* start = */ stack_addr,
        /* end = */ static_cast<char*>(stack_addr) - size,
        /* size = */ Bytes(size)};
  }
#endif // If macOS.
}

////////////////////////////////////////////////////////////////////////

inline void CheckSufficientStackSpace(const size_t size) {
  // NOTE: making this a 'thread_local' so we only compute it once!
  static thread_local StackInfo stack_info = GetStackInfo();

  const Bytes available = stack_info.StackAvailable();

  // NOTE: we determine sufficient stack space as follows. Assume
  // that for any continuation we may need at least two of them in
  // an unoptimized build, one for the caller and one for the
  // callee, plus we should have at least as much as a page size for
  // a buffer.
  bool has_sufficient_stack_space =
      available.bytes() > (size * 2) + 4096;

  CHECK(has_sufficient_stack_space)
      << "\n"
      << "\n"
      << "You've got a large continuation that may exceed the available\n"
      << "space on the stack!\n"
      << "\n"
      << "It looks like your stack size is: "
      << stack_info.size << "\n"
      << (stack_info.size < Megabytes(8)
              ? "\nWe recommend stack sizes that are at least 8Mb\n"
              : std::string("\n"))
      << "\n"
      << "Alternatively if you happen to have an extra large continuation\n"
      << "consider type-erasing it with 'Task' or 'Generator' so that it\n"
      << "doesn't get allocated on the stack!\n"
      << "\n";
}

////////////////////////////////////////////////////////////////////////

class Thread {
 public:
  Thread() = default;

  ~Thread() noexcept {
    CHECK(!joinable_) << "A thread was left not joined/not detached";
  }

  Thread(const Thread& other) = delete;

  // IMPORTANT NOTE: on macos stacksize should be a multiple of the system
  // page size!!!
  // (check_line_length skip)
  // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/pthread_attr_setstacksize.3.html
  template <typename Callable>
  Thread(
      Callable&& callable,
      const std::string& name,
      const Bytes& stack_size = Megabytes(8))
    : joinable_{true} {
    CHECK_GE(stack_size.bytes(), PTHREAD_STACK_MIN)
        << "Stack size should not be less than"
           " the system-defined minimum size";

    pthread_attr_t attr;

    PCHECK(pthread_attr_init(&attr) == 0)
        << "Failed to initialize thread attributes via "
           "'pthread_attr_init(...)'";

    // IMPORTANT NOTE: on macos stacksize should be a multiple of the system
    // page size, otherwise `pthread_attr_setstacksize` will fail.
    // (check_line_length skip)
    // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/pthread_attr_setstacksize.3.html
    PCHECK(pthread_attr_setstacksize(&attr, stack_size.bytes()) == 0)
        << "Failed to set the stack size via 'pthread_attr_setstacksize'"
           " (if you are on macOS - probably you are trying"
           " to set the stack size which is not a multiple"
           " of the system page size)";

    struct Data {
      std::string thread_name{};
      Callable callable;
      Data(const std::string& name, Callable&& f)
        : thread_name{name},
          callable{std::forward<Callable>(f)} {}
    };

    PCHECK(pthread_create(
               &thread_handle_,
               &attr,
               +[](void* arg) -> void* {
                 Data* data = reinterpret_cast<Data*>(arg);

                 PCHECK(
                     pthread_setname_np(
#ifdef __linux__
                         pthread_self(),
#endif
                         data->thread_name.c_str())
                     == 0)
                     << "Failed to set thread name via "
                        "'pthread_setname_np(...)'";

                 try {
                   data->callable();
                 } catch (const std::exception& e) {
                   LOG(FATAL) << "Caught exception while running thread '"
                              << data->thread_name << "': " << e.what();
                 } catch (...) {
                   LOG(FATAL) << "Caught unknown exception while running"
                              << "thread '" << data->thread_name << "'";
                 }
                 delete data;
                 return nullptr;
               },
               new Data(name, std::forward<Callable>(callable)))
           == 0)
        << "Failed to create a new thread via 'pthread_create'";

    PCHECK(pthread_attr_destroy(&attr) == 0)
        << "Failed to destroy thread attributes via "
           "'pthread_attr_destroy(...)'";
  }

  Thread(Thread&& that) noexcept {
    *this = std::move(that);
  }

  Thread& operator=(const Thread& other) = delete;

  Thread& operator=(Thread&& that) noexcept {
    if (this == &that) {
      return *this;
    }

    CHECK(!joinable_) << "Thread not joined nor detached";

    thread_handle_ = std::exchange(that.thread_handle_, pthread_t{});
    joinable_ = std::exchange(that.joinable_, false);

    return *this;
  }

  pthread_t native_handle() const {
    return thread_handle_;
  }

  bool is_joinable() const {
    return joinable_;
  }

  void join() {
    if (joinable_) {
      PCHECK(pthread_join(thread_handle_, nullptr) == 0)
          << "Failed to join thread via 'pthread_join(...)'";
    }
    joinable_ = false;
  }

  void detach() {
    CHECK(joinable_)
        << "Trying to detach already joined/detached thread";

    joinable_ = false;

    PCHECK(pthread_detach(thread_handle_) == 0)
        << "Failed to detach thread via 'pthread_detach(...)'";
  }

  bool is_current_thread() {
    return pthread_equal(pthread_self(), thread_handle_);
  }

 private:
  // For Linux `pthread_t` is unsigned long, for
  // macOS this is an `_opaque_pthread_t` struct.
  // https://opensource.apple.com/source/xnu/xnu-517.7.7/bsd/sys/types.h
  pthread_t thread_handle_{};
  bool joinable_ = false;
};

////////////////////////////////////////////////////////////////////////

#else // Windows.

////////////////////////////////////////////////////////////////////////

inline void CheckSufficientStackSpace(const size_t size) {}

////////////////////////////////////////////////////////////////////////

// We are using 'pthread' for Linux and MacOS, which is not supported by
// Windows, so we instead use 'std::thread'.
class Thread {
 public:
  Thread() = default;

  template <typename Callable>
  Thread(
      Callable&& callable,
      const std::string& name,
      const Bytes& stack_size = Megabytes(8))
    : thread_(std::move(callable)) {
    // Stack managment for Windows is not supported yet.
  }

  Thread(const Thread& other) = delete;

  Thread& operator=(Thread&& that) noexcept {
    if (this == &that) {
      return *this;
    }

    CHECK(!thread_.joinable()) << "Thread not joined nor detached";

    thread_ = std::move(that.thread_);

    return *this;
  }

  ~Thread() noexcept {
    CHECK(!thread_.joinable()) << "A thread was left not joined/not detached";
  }

  bool is_joinable() const {
    return thread_.joinable();
  }

  void join() {
    CHECK(thread_.joinable())
        << "Trying to join already joined/detached thread";
    thread_.join();
  }

  void detach() {
    CHECK(thread_.joinable())
        << "Trying to detach already joined/detached thread";
    thread_.detach();
  }

  bool is_current_thread() {
    return thread_.get_id() == std::this_thread::get_id();
  }

 private:
  std::thread thread_;
};

////////////////////////////////////////////////////////////////////////

#endif

////////////////////////////////////////////////////////////////////////

} // namespace os

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
