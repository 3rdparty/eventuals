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
};

////////////////////////////////////////////////////////////////////////

inline StackInfo GetStackInfo(bool stack_grows_downward = true) {
#ifdef __linux__
  void* stack_addr = nullptr;
  size_t size = 0;
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
  pthread_t self = pthread_self();

  // For macOS `pthread_get_stackaddr_np` can return either the base
  // or the end of the stack.
  // Check 1274 line on the link below:
  // https://android.googlesource.com/platform/art/+/master/runtime/thread.cc
  void* stack_addr = pthread_get_stackaddr_np(self);

  size_t size = pthread_get_stacksize_np(self);
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
  void* end = stack_addr;
  void* start = static_cast<char*>(stack_addr) + size;
  [[maybe_unused]] char local_var{};
#endif // If Linux.

#ifdef __MACH__
  void* start = nullptr;
  void* end = nullptr;
  [[maybe_unused]] char local_var{};

  // If the address of a local variable is greater than the stack
  // address from `pthread_get_stacksize_np` ->
  // `pthread_get_stacksize_np` returns the end of the stack since
  // on x86 architecture the stack grows downward (from highest to
  // lowest address).
  if (&local_var > stack_addr) {
    start = static_cast<char*>(stack_addr) - size;
    end = stack_addr;
  } else {
    start = stack_addr;
    end = static_cast<char*>(stack_addr) - size;
  }
#endif // If macOS.
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
// Implementation might be added in future.
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__)
  || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__)
      || defined(_ARCH_PPC)
// Implementation might be added in future.
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
// Implementation might be added in future.
#elif defined(__sparc__) || defined(__sparc)
// Implementation might be added in future.
#elif defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
// Stack direction growth for arm architecture is selectable.
// (check_line_length skip)
// https://stackoverflow.com/questions/19070095/who-selects-the-arm-stack-direction
#ifdef __linux__
  void* start = nullptr;
  void* end = nullptr;

  // The function `pthread_attr_getstack` on Linux returns
  // the lowest stack address. So, `stack_addr` will always
  // have the lowest address.
  if (stack_grows_downward) {
    end = stack_addr;
    start = static_cast<char*>(stack_addr) + size;
  } else {
    start = stack_addr;
    end = static_cast<char*>(stack_addr) + size;
  }
#endif // If Linux.

#ifdef __MACH__
  void* start = nullptr;
  void* end = nullptr;
  char local_var{};

  // For macOS `stack_addr` can be either the base or the end of the stack.
  if (&local_var > stack_addr && stack_grows_downward) {
    start = static_cast<char*>(stack_addr) - size;
    end = stack_addr;
  } else if (&local_var > stack_addr && !stack_grows_downward) {
    end = static_cast<char*>(stack_addr) + size;
    start = stack_addr;
  } else if (stack_addr > &local_var && stack_grows_downward) {
    end = static_cast<char*>(stack_addr) - size;
    start = stack_addr;
  } else {
    end = stack_addr;
    start = static_cast<char*>(stack_addr) - size;
  }
#endif // If macOS.
#endif // If ARM.

  CHECK(start)
      << "Illegal stack start";

  CHECK(end)
      << "Illegal stack end";

  CHECK(size)
      << "Stack size must not be null";

  return StackInfo{start, end, Bytes{size}};
}

////////////////////////////////////////////////////////////////////////

inline Bytes StackAvailable(
    void* stack_start,
    void* stack_end,
    bool stack_grows_downward = true) {
  [[maybe_unused]] char local_var{};
#if defined(__x86_64__)
  // For this architecture stack grows downward.
  return Bytes((&local_var) - ((char*) stack_end) - sizeof(local_var));
#elif defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  if (stack_grows_downward) {
    return Bytes((&local_var) - ((char*) stack_end) - sizeof(local_var));
  } else {
    return Bytes(((char*) stack_end) - (&local_var));
  }
#else
  return Bytes{0};
#endif
}

////////////////////////////////////////////////////////////////////////

// Function that returns true if the stack grows downward. If the stack
// grows upward - returns false.
inline bool StackGrowsDownward(char* var) {
  char local_var{};
  if (var < &local_var) {
    return false;
  } else {
    return true;
  }
}

////////////////////////////////////////////////////////////////////////

inline void CheckSufficientStackSpace(const size_t size) {
  // A simple char variable for checking growth direction of stack;
  char var{};
  static thread_local StackInfo stack_info =
      GetStackInfo(StackGrowsDownward(&var));

  const Bytes available = StackAvailable(stack_info.start, stack_info.end);

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
