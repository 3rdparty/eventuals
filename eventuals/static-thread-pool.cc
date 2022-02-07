#include "eventuals/static-thread-pool.h"

#include "eventuals/os.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

StaticThreadPool::StaticThreadPool()
  : concurrency(std::thread::hardware_concurrency()) {
  semaphores_.reserve(concurrency);
  heads_.reserve(concurrency);
  threads_.reserve(concurrency);
  for (size_t cpu = 0; cpu < concurrency; cpu++) {
    semaphores_.emplace_back();
    heads_.emplace_back();
    ready_.emplace_back();
    threads_.emplace_back(
        [this, cpu]() {
          StaticThreadPool::member = true;
          StaticThreadPool::cpu = cpu;

          SetAffinity(threads_[cpu], cpu);

          EVENTUALS_LOG(3)
              << "Thread " << cpu << " (id=" << std::this_thread::get_id()
              << ") is running on core " << GetRunningCPU();

          // NOTE: we store each 'semaphore' and 'head' in each thread
          // so as to hopefully get less false sharing when other
          // threads are trying to enqueue a waiter.
          Semaphore semaphore;
          std::atomic<Context*> head = nullptr;

          semaphores_[cpu] = &semaphore;
          heads_[cpu] = &head;

          ready_[cpu].Signal();

          do {
            semaphore.Wait();

          load:
            auto* context = head.load(std::memory_order_relaxed);

            if (context != nullptr) {
              if (context->next == nullptr) {
                if (!head.compare_exchange_weak(
                        context,
                        nullptr,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                  goto load; // Try again.
                }
              } else {
                while (context->next->next != nullptr) {
                  context = context->next;
                }

                assert(context->next != nullptr);

                auto* next = context->next;
                context->next = nullptr;
                context = next;
              }

              CHECK_NOTNULL(context);

              CHECK_EQ(nullptr, context->next);

              Context::Set(context);

              context->unblock();

              EVENTUALS_LOG(1) << "Resuming '" << context->name() << "'";

              CHECK(context->callback);
              context->callback();

              CHECK_EQ(context, Context::Get());

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
  CHECK(!context->blocked()) << context->name();
  CHECK(context->next == nullptr) << context->name();

  EVENTUALS_LOG(1) << "Submitting '" << context->name() << "'";

  auto* requirements =
      static_cast<StaticThreadPool::Requirements*>(context->data);
  auto& pinned = requirements->pinned;

  CHECK(pinned.cpu()) << context->name();

  unsigned int cpu = pinned.cpu().value();

  assert(cpu < concurrency);

  context->block();

  context->callback = std::move(callback);

  auto* head = heads_[cpu];

  context->next = head->load(std::memory_order_relaxed);

  while (!head->compare_exchange_weak(
      context->next,
      context,
      std::memory_order_release,
      std::memory_order_relaxed)) {}

  auto* semaphore = semaphores_[cpu];

  semaphore->Signal();
}

////////////////////////////////////////////////////////////////////////

bool StaticThreadPool::Continuable(Context* context) {
  CHECK(!context->blocked()) << context->name();
  CHECK(context->next == nullptr) << context->name();

  auto* requirements =
      static_cast<StaticThreadPool::Requirements*>(context->data);
  auto& pinned = requirements->pinned;

  CHECK(pinned.cpu()) << context->name();

  unsigned int cpu = pinned.cpu().value();

  return StaticThreadPool::member && StaticThreadPool::cpu == cpu;
}

////////////////////////////////////////////////////////////////////////

void StaticThreadPool::Clone(Context* child) {
  // We copy the parent's data pointer which points to the 'Requirements'.
  // We don't need to reallocate the pointer to 'Requirements' because it must
  // outlive the parent context and the parent context must outlive this child
  // context.
  child->data = CHECK_NOTNULL(Scheduler::Context::Get()->data);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
