#pragma once

#include <thread>

#include "glog/logging.h" // NOTE: must be included before <windows.h>.

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h> // For PTHREAD_STACK_MIN.
#include <pthread.h>

#include <functional>
#include <limits>

#include "stout/bytes.h"
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
    [[maybe_unused]] char local_var{};
#if defined(__x86_64__)
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
#endif // If Linux.

#ifdef __MACH__
  stack_addr = pthread_get_stackaddr_np(self);
  size = pthread_get_stacksize_np(self);
#endif // If macOS.

#if defined(__x86_64__)
#ifdef __linux__
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
  // or the end of the stack.
  // Check 1274 line on the link below:
  // https://android.googlesource.com/platform/art/+/master/runtime/thread.cc

  // If the address of a local variable is greater than the stack
  // address from `pthread_get_stacksize_np` ->
  // `pthread_get_stacksize_np` returns the end of the stack since
  // on x86 architecture the stack grows downward (from highest to
  // lowest address).
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
#endif
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
#else // If Windows.
inline void CheckSufficientStackSpace(const size_t size) {}
#endif

////////////////////////////////////////////////////////////////////////

} // namespace os

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
