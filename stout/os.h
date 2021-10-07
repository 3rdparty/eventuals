#pragma once

#include <thread>

#include "glog/logging.h" // NOTE: must be included before <windows.h>.

#ifdef __MACH__
#include <limits>
#elif _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif // __MACH__

////////////////////////////////////////////////////////////////////////

namespace stout {

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

} // namespace stout

////////////////////////////////////////////////////////////////////////
