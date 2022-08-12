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

#ifdef __linux__
inline Bytes GetStackSize() {
  size_t stack_size{};
  pthread_attr_t attr;

  PCHECK(pthread_attr_init(&attr) == 0)
      << "Failed to initialize the thread "
         "attributes object referred to by attr via 'pthread_attr_init(...)' ";

  PCHECK(pthread_attr_getstacksize(&attr, &stack_size) == 0)
      << "Failed to get the stack size via "
         "'pthread_attr_getstacksize(...)'";

  PCHECK(pthread_attr_destroy(&attr) == 0)
      << "Failed to destroy thread attributes via "
         "'pthread_attr_destroy(...)'";

  return Bytes(stack_size);
}

inline void* GetStackBase() {
  pthread_attr_t attr;
  void* stack_base{};
  size_t stack_size{};

  PCHECK(pthread_getattr_np(pthread_self(), &attr) == 0)
      << "Failed to initialize the thread attributes"
         "object referred to by attr via 'pthread_getattr_np(...)' ";

  PCHECK(pthread_attr_getstack(&attr, &stack_base, &stack_size) == 0)
      << "Failed to get stack address via 'pthread_attr_getstack(...)'";

  PCHECK(pthread_attr_destroy(&attr) == 0)
      << "Failed to destroy thread attributes via "
         "'pthread_attr_destroy(...)'";

  return stack_base;
}
#endif

#ifdef __MACH__
inline Bytes GetStackSize() {
  return Bytes(pthread_get_stacksize_np(pthread_self()));
}

// For macOS `pthread_get_stackaddr_np` can return either the base
// or the end of the stack.
inline void* GetStackBase() {
  return pthread_get_stackaddr_np(pthread_self());
}
#endif

#if defined(__x86_64__)
// Implementation for Windows might be added in future for this architecture.
// For x86 stack grows downward (from highest address to lowest).
// (check_line_length skip)
// https://eli.thegreenplace.net/2011/02/04/where-the-top-of-the-stack-is-on-x86/
// So the stack end will be the lowest address.
#ifdef __linux__
inline void*
GetStackStart(
    void* stack_base,
    const Bytes& stack_size) {
  return (char*) stack_base + stack_size.bytes();
}

inline void* GetStackEnd(
    void* stack_base,
    const Bytes& stack_size) {
  return stack_base;
}
#endif

#ifdef __MACH__
inline void* GetStackStart(
    void* stack_base,
    const Bytes& stack_size) {
  [[maybe_unused]] int local_var{};
  if (&local_var > stack_base) {
    return (char*) stack_base - stack_size.bytes();
  } else {
    return stack_base;
  }
}

inline void* GetStackEnd(
    void* stack_base,
    const Bytes& stack_size) {
  [[maybe_unused]] int local_var{};
  if (&local_var > stack_base) {
    return stack_base;
  } else {
    return (char*) stack_base - stack_size.bytes();
  }
}
#endif

#ifndef _WIN32
inline Bytes Available(
    void* stack_start,
    void* stack_end,
    const size_t* latest_local_variable) {
  return Bytes(
      ((char*) latest_local_variable)
      - ((char*) stack_end) - sizeof(latest_local_variable));
}
#endif
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
// Implementation for checking sufficient stack space might be added in future.
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__)
|| defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__)
    || defined(_ARCH_PPC)
// Implementation for checking sufficient stack space might be added in future.
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
// Implementation for checking sufficient stack space might be added in future.
#elif defined(__sparc__) || defined(__sparc)
// Implementation for checking sufficient stack space might be added in future.
#elif defined(__arm__) || defined(__arm64__)
// Implementation for checking sufficient stack space might be added in future.
#endif

////////////////////////////////////////////////////////////////////////

#ifndef _WIN32
inline void CheckSufficientStackSpace(const size_t composed_size) {
  void *stack_start{}, *stack_end{}, *stack_base{};
  Bytes stack_size = GetStackSize();
  stack_base = GetStackBase();
  stack_start = GetStackStart(stack_base, stack_size);
  stack_end = GetStackEnd(stack_base, stack_size);

  const size_t size = composed_size;

  const Bytes available = Available(stack_start, stack_end, &size);

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
      << std::to_string(stack_size.bytes() * 0.000001) << "Mb\n"
      << (stack_size < Megabytes(8)
              ? "\nWe recommend stack sizes that are at least 8Mb\n"
              : std::string("\n"))
      << "\n"
      << "Alternatively if you happen to have an extra large continuation\n"
      << "consider type-erasing it with 'Task' or 'Generator' so that it\n"
      << "doesn't get allocated on the stack!\n"
      << "\n";
}
#endif

////////////////////////////////////////////////////////////////////////

} // namespace os

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
