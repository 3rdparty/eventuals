#pragma once

#include "glog/logging.h" // NOTE: must be included before <windows.h>.

#ifdef __MACH__
#include <mach/mach.h>
#elif _WIN32
#include <windows.h>
#else
#include <semaphore.h>
#endif // __MACH__

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// TODO(benh): Add tests!

#ifdef __MACH__
class Semaphore {
 public:
  Semaphore() {
    CHECK_EQ(
        KERN_SUCCESS,
        semaphore_create(mach_task_self(), &semaphore, SYNC_POLICY_FIFO, 0));
  }

  Semaphore(const Semaphore& other) = delete;

  ~Semaphore() {
    CHECK_EQ(KERN_SUCCESS, semaphore_destroy(mach_task_self(), semaphore));
  }

  Semaphore& operator=(const Semaphore& other) = delete;

  void Wait() {
    CHECK_EQ(KERN_SUCCESS, semaphore_wait(semaphore));
  }

  void Signal() {
    CHECK_EQ(KERN_SUCCESS, semaphore_signal(semaphore));
  }

 private:
  semaphore_t semaphore;
};
#elif _WIN32
class Semaphore {
 public:
  Semaphore() {
    semaphore = CHECK_NOTNULL(CreateSemaphore(nullptr, 0, LONG_MAX, nullptr));
  }

  Semaphore(const Semaphore& other) = delete;

  ~Semaphore() {
    CHECK(CloseHandle(semaphore));
  }

  Semaphore& operator=(const Semaphore& other) = delete;

  void Wait() {
    CHECK_EQ(WAIT_OBJECT_0, WaitForSingleObject(semaphore, INFINITE));
  }

  void Signal() {
    CHECK(ReleaseSemaphore(semaphore, 1, nullptr));
  }

 private:
  HANDLE semaphore;
};
#else
class Semaphore {
 public:
  Semaphore() {
    PCHECK(sem_init(&semaphore, 0, 0) == 0);
  }

  Semaphore(const Semaphore& other) = delete;

  ~Semaphore() {
    PCHECK(sem_destroy(&semaphore) == 0);
  }

  Semaphore& operator=(const Semaphore& other) = delete;

  void Wait() {
    int result = sem_wait(&semaphore);

    while (result != 0 && errno == EINTR) {
      result = sem_wait(&semaphore);
    }

    PCHECK(result == 0);
  }

  void Signal() {
    PCHECK(sem_post(&semaphore) == 0);
  }

 private:
  sem_t semaphore;
};
#endif // __MACH__

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
