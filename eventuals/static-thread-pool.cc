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
          std::atomic<Waiter*> head = nullptr;

          semaphores_[cpu] = &semaphore;
          heads_[cpu] = &head;

          ready_[cpu].Signal();

          do {
            semaphore.Wait();

          load:
            Waiter* waiter = head.load(std::memory_order_relaxed);

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

              // NOTE: explicitly not borrowing the context here
              // because we don't want to prevent the destruction of
              // the context after we invoke the callback below.
              Context* context = CHECK_NOTNULL(waiter->context.get());

              EVENTUALS_LOG(1) << "Resuming '" << context->name() << "'";

              Context::Unblock(Borrow(*context));

              // // TODO(benh): can't we just use 'Context::Unblock()'
              // // here because shouldn't we be on the default context?
              // context->unblock();

              // stout::borrowed_ref<Context> previous =
              //     Reborrow(Context::Switch(Borrow(*context)));

              // NOTE: need to relinquish borrow of context now rather
              // than waiting for the destructor to avoid a possile
              // deadlock when trying to destruct the context.
              waiter->context.relinquish();

              CHECK(waiter->callback);

              Callback<void()> callback = std::move(waiter->callback);

              callback();

              //////////////////////////////////////////////////////////
              // NOTE: can't use 'waiter' or 'context' at this  point //
              // in time because it might have been deallocated!      //
              //////////////////////////////////////////////////////////

              // Context::Switch(std::move(previous));

              Context::Switch(Borrow(Context::Default()));

              // TODO(benh): check that the returned context pointer
              // is the same as what we switched to (but nothing more
              // because it might have been deallocated) or is the
              // default context because the context blocked (in which
              // case we can check if it's blocked because we're the
              // only ones that would unblock and run it!)
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

void StaticThreadPool::Submit(Callback<void()> callback, Context& context) {
  EVENTUALS_LOG(1) << "Submitting '" << context.name() << "'";

  CHECK(!context.blocked()) << context.name();

  CHECK_EQ(this, context.scheduler());

  auto* requirements =
      static_cast<StaticThreadPool::Requirements*>(context.data);

  auto& pinned = requirements->pinned;

  CHECK(pinned.cpu()) << context.name();

  unsigned int cpu = pinned.cpu().value();

  assert(cpu < concurrency);

  context.block();

  Waiter* waiter = &context.waiter;

  waiter->context = Borrow(context);

  waiter->callback = std::move(callback);

  auto* head = heads_[cpu];

  CHECK(waiter->next == nullptr) << context.name();

  waiter->next = head->load(std::memory_order_relaxed);

  while (!head->compare_exchange_weak(
      waiter->next,
      waiter,
      std::memory_order_release,
      std::memory_order_relaxed)) {}

  auto* semaphore = semaphores_[cpu];

  semaphore->Signal();
}

////////////////////////////////////////////////////////////////////////

bool StaticThreadPool::Continuable(const Context& context) {
  CHECK(!context.blocked()) << context.name();

  CHECK(context.waiter.next == nullptr) << context.name();

  CHECK_EQ(this, context.scheduler());

  auto* requirements =
      static_cast<StaticThreadPool::Requirements*>(context.data);

  auto& pinned = requirements->pinned;

  CHECK(pinned.cpu()) << context.name();

  unsigned int cpu = pinned.cpu().value();

  return StaticThreadPool::member && StaticThreadPool::cpu == cpu;
}

////////////////////////////////////////////////////////////////////////

void StaticThreadPool::Clone(Context& child) {
  // We copy the parent's data pointer which points to the
  // 'Requirements'.  We don't need to reallocate the pointer to
  // 'Requirements' because it must outlive the parent context and the
  // parent context must outlive this child context.
  child.data = CHECK_NOTNULL(Scheduler::Context::Get()->data);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
