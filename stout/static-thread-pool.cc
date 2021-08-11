#include "stout/static-thread-pool.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

StaticThreadPool::StaticThreadPool()
  : concurrency(std::thread::hardware_concurrency()) {
  semaphores_.reserve(concurrency);
  heads_.reserve(concurrency);
  threads_.reserve(concurrency);
  for (size_t core = 0; core < concurrency; core++) {
    semaphores_.emplace_back();
    heads_.emplace_back();
    ready_.emplace_back();
    threads_.emplace_back(
        [this, core]() {
          StaticThreadPool::member = true;
          StaticThreadPool::core = core;

          // NOTE: we store each 'semaphore' and 'head' in each thread
          // so as to hopefully get less false sharing when other
          // threads are trying to enqueue a waiter.
          Semaphore semaphore;
          std::atomic<Waiter*> head = nullptr;

          semaphores_[core] = &semaphore;
          heads_[core] = &head;

          ready_[core].Signal();

          do {
            semaphore.Wait();

          load:
            auto* waiter = head.load(std::memory_order_relaxed);

            if (waiter != nullptr) {
              if (waiter->next == nullptr) {
                if (!head.compare_exchange_weak(
                        waiter,
                        nullptr,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                  goto load; // Try again.
                }
              } else {
                while (waiter->next->next != nullptr) {
                  waiter = waiter->next;
                }

                assert(waiter->next != nullptr);

                auto* next = waiter->next;
                waiter->next = nullptr;
                waiter = next;
              }

              CHECK_NOTNULL(waiter);

              CHECK_EQ(nullptr, waiter->next);

              Context::Set(waiter);

              waiter->waiting = false;

              STOUT_EVENTUALS_LOG(1) << "Resuming '" << waiter->name() << "'";

              CHECK(waiter->callback);
              waiter->callback();

              CHECK_EQ(waiter, Context::Get());

              ////////////////////////////////////////////////////
              // NOTE: can't use 'waiter' at this point in time //
              // because it might have been deallocated!        //
              ////////////////////////////////////////////////////
            }
          } while (!shutdown_.load());
        });
  }

  for (size_t core = 0; core < concurrency; core++) {
    ready_[core].Wait();
  }
}

////////////////////////////////////////////////////////////////////////

StaticThreadPool::~StaticThreadPool() {
  shutdown_.store(true);
  while (!threads_.empty()) {
    auto* semaphore = semaphores_.back();
    semaphore->Signal();
    semaphores_.pop_back();
    auto& thread = threads_.back();
    thread.join();
    threads_.pop_back();
  }
}

////////////////////////////////////////////////////////////////////////

void StaticThreadPool::Submit(Callback<> callback, Context* context) {
  auto* waiter = static_cast<Waiter*>(CHECK_NOTNULL(context));

  CHECK(!waiter->waiting) << waiter->name();
  CHECK(waiter->next == nullptr) << waiter->name();

  STOUT_EVENTUALS_LOG(1) << "Submitting '" << waiter->name() << "'";

  auto& pinned = waiter->requirements()->pinned;

  if (!pinned.core) {
    pinned.core = next_++ % concurrency;
  }

  unsigned int core = pinned.core.value();

  assert(core < concurrency);

  waiter->waiting = true;

  waiter->callback = std::move(callback);

  auto* head = heads_[core];

  waiter->next = head->load(std::memory_order_relaxed);

  while (!head->compare_exchange_weak(
      waiter->next,
      waiter,
      std::memory_order_release,
      std::memory_order_relaxed)) {}

  auto* semaphore = semaphores_[core];

  semaphore->Signal();
}

////////////////////////////////////////////////////////////////////////

bool StaticThreadPool::Continue(Context* context) {
  auto* waiter = static_cast<Waiter*>(CHECK_NOTNULL(context));

  CHECK(!waiter->waiting) << waiter->name();
  CHECK(waiter->next == nullptr) << waiter->name();

  auto& pinned = waiter->requirements()->pinned;

  CHECK(pinned.core) << waiter->name();

  unsigned int core = pinned.core.value();

  return StaticThreadPool::member && StaticThreadPool::core == core;
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
