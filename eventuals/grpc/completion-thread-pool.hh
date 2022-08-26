#pragma once

#include <deque>
#include <future>
#include <optional>
#include <thread>

#include "eventuals/callback.hh"
#include "eventuals/semaphore.hh"
#include "grpcpp/completion_queue.h"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

template <typename CompletionQueue>
class CompletionThreadPool {
 public:
  virtual ~CompletionThreadPool() = default;

  virtual void AddCompletionQueue(std::unique_ptr<CompletionQueue>&& cq) = 0;

  virtual size_t NumberOfCompletionQueues() = 0;

  virtual stout::borrowed_ref<CompletionQueue> Schedule() = 0;
};

////////////////////////////////////////////////////////////////////////

// TODO(benh): 'DynamicCompletionThreadPool' which takes both a
// minimum and a maximum number of threads per completion queue and
// adds or removes them (or puts some to sleep) when they are
// unnecessary. This is unlikely to be useful for eventuals only
// codebases but may be useful if there are some calls to code that
// can not be called asynchronously and block.

////////////////////////////////////////////////////////////////////////

// A completion thread pool with a static number of threads per
// completion queue.
//
// NOTE: to be thread-safe you *MUST* have only one thread that calls
// 'AddCompletionQueue()' and then you may have as many threads as you
// want call 'Schedule()' and only one thread should call 'Shutdown()'
// and 'Wait()' (or just let the destructor do that for you).
template <typename CompletionQueue>
class StaticCompletionThreadPool
  : public CompletionThreadPool<CompletionQueue> {
 public:
  StaticCompletionThreadPool(
      std::vector<std::unique_ptr<CompletionQueue>>&& cqs,
      unsigned int number_of_threads_per_completion_queue = 1);

  StaticCompletionThreadPool(
      unsigned int number_of_completion_queues =
          std::thread::hardware_concurrency(),
      unsigned int number_of_threads_per_completion_queue = 1);

  StaticCompletionThreadPool(StaticCompletionThreadPool&& that) = default;

  ~StaticCompletionThreadPool() override {
    Shutdown();
    Wait();
  }

  void Shutdown() {
    if (!shutdown_) {
      for (stout::Borrowable<std::unique_ptr<CompletionQueue>>& cq : cqs_) {
        cq->Shutdown();
      }
      shutdown_ = true;
    }
  }

  void Wait() {
    while (!threads_.empty()) {
      std::thread& thread = threads_.back();

      thread.join();

      threads_.pop_back();

      stout::Borrowable<std::unique_ptr<CompletionQueue>>& cq = cqs_.back();

      void* tag = nullptr;
      bool ok = false;
      while (cq->Next(&tag, &ok)) {}

      cqs_.pop_back();
    }
  }

  void AddCompletionQueue(std::unique_ptr<CompletionQueue>&& cq) override {
    CHECK(!scheduling_)
        << "\n"
        << "\n"
        << "It is currently *NOT* safe to call 'AddCompletionQueue()' after\n"
        << "starting to make calls to 'Schedule()'. You should add all of\n"
        << "your completion queues first and then once you start calling\n"
        << "'Schedule()' you should not add any more!\n"
        << "\n";

    cqs_.emplace_back(std::move(cq));
    for (size_t i = 0;
         i < number_of_threads_per_completion_queue_;
         ++i) {
      threads_.emplace_back(
          [cq = cqs_.back().get()]() {
            void* tag = nullptr;
            bool ok = false;
            while (cq->Next(&tag, &ok)) {
              (*static_cast<Callback<void(bool)>*>(tag))(ok);
            }
          });
    }
  }

  size_t NumberOfCompletionQueues() override {
    return cqs_.size();
  }

  stout::borrowed_ref<CompletionQueue> Schedule() override {
    scheduling_ = true;

    // TODO(benh): provide alternative "scheduling" algorithms in
    // addition to "least loaded", e.g., round-robin, random, but
    // careful not to break anyone that currently assumes the "least
    // laoded" semantics!
    stout::Borrowable<std::unique_ptr<CompletionQueue>>* selected = nullptr;
    size_t load = SIZE_MAX;
    for (stout::Borrowable<std::unique_ptr<CompletionQueue>>& cq : cqs_) {
      size_t borrows = cq.borrows();
      if (borrows < load) {
        selected = &cq;
        load = borrows;
      }
    }
    CHECK(selected != nullptr);
    return selected->Borrow();
  }

 private:
  std::deque<stout::Borrowable<std::unique_ptr<CompletionQueue>>> cqs_;

  size_t number_of_threads_per_completion_queue_ = 1;

  std::vector<std::thread> threads_;

  bool scheduling_ = false;
  bool shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

template <typename CompletionQueue>
StaticCompletionThreadPool<CompletionQueue>::
    StaticCompletionThreadPool(
        std::vector<std::unique_ptr<CompletionQueue>>&& cqs,
        unsigned int number_of_threads_per_completion_queue)
  : number_of_threads_per_completion_queue_(
      number_of_threads_per_completion_queue) {
  threads_.reserve(cqs.size() * number_of_threads_per_completion_queue);
  for (std::unique_ptr<CompletionQueue>& cq : cqs) {
    AddCompletionQueue(std::move(cq));
  }
}

////////////////////////////////////////////////////////////////////////

template <>
inline StaticCompletionThreadPool<::grpc::CompletionQueue>::
    StaticCompletionThreadPool(
        unsigned int number_of_completion_queues,
        unsigned int number_of_threads_per_completion_queue)
  : StaticCompletionThreadPool(
      [&number_of_completion_queues]() {
        std::vector<std::unique_ptr<::grpc::CompletionQueue>> cqs;
        for (size_t i = 0; i < number_of_completion_queues; i++) {
          cqs.emplace_back(std::make_unique<::grpc::CompletionQueue>());
        }
        return cqs;
      }(),
      number_of_threads_per_completion_queue) {}

////////////////////////////////////////////////////////////////////////

// '::grpc::ServerCompletionQueue' is not public!
template <>
StaticCompletionThreadPool<::grpc::ServerCompletionQueue>::
    StaticCompletionThreadPool(
        unsigned int number_of_completion_queues,
        unsigned int number_of_threads_per_completion_queue) = delete;

////////////////////////////////////////////////////////////////////////

using ClientCompletionThreadPool =
    StaticCompletionThreadPool<::grpc::CompletionQueue>;

////////////////////////////////////////////////////////////////////////

using ServerCompletionThreadPool =
    StaticCompletionThreadPool<::grpc::ServerCompletionQueue>;

////////////////////////////////////////////////////////////////////////

// Helper for writing deterministic tests that require specific
// orderings to be true.
//
// This thread pool starts off with a single thread that is _running_,
// just like an 'EventLoop' starts of with the 'Clock' _not_
// paused. You need to 'Pause()' this thread pool before making any
// gRPC calls if you don't want anything to happen until you call one
// of the 'RunUntil()' functions.
//
// NOTE: you must call 'Resume()' _before_ your test finishes or else
// you might deadlock with '~Server()'. If you explicitly call
// 'Server::Shutdown()' then you must 'Resume()' before that call.
class TestingCompletionThreadPool {
 public:
  TestingCompletionThreadPool();
  ~TestingCompletionThreadPool();

  void Pause();
  void Resume();

  stout::borrowed_ref<CompletionThreadPool<::grpc::ServerCompletionQueue>>
  ServerCompletionThreadPool();

  stout::borrowed_ref<CompletionThreadPool<::grpc::CompletionQueue>>
  ClientCompletionThreadPool();

  bool RunUntilIdle();

 private:
  class ClientCompletionThreadPoolProxy
    : public CompletionThreadPool<::grpc::CompletionQueue> {
   public:
    ClientCompletionThreadPoolProxy(
        stout::borrowed_ref<::grpc::CompletionQueue>&& cq)
      : cq_(std::move(cq)) {}

    void AddCompletionQueue(
        std::unique_ptr<::grpc::CompletionQueue>&& cq) override {
      LOG(FATAL)
          << "You can not add completion "
          << "queues to a 'TestingCompletionThreadPool'";
    }

    size_t NumberOfCompletionQueues() override {
      return 1;
    }

    stout::borrowed_ref<::grpc::CompletionQueue> Schedule() override {
      return cq_.reborrow();
    }

   private:
    stout::borrowed_ref<::grpc::CompletionQueue> cq_;
  };

  class ServerCompletionThreadPoolProxy
    : public CompletionThreadPool<::grpc::ServerCompletionQueue> {
   public:
    // NOTE: we "borrow" a reference to our outer class
    // 'TestingCompletionThreadPool' but we don't bother using
    // 'stout::borrowed_ref' because the outer class will always
    // outlive the proxy.
    ServerCompletionThreadPoolProxy(TestingCompletionThreadPool& pool)
      : pool_(pool) {}

    void AddCompletionQueue(
        std::unique_ptr<::grpc::ServerCompletionQueue>&& cq) override {
      CHECK(!pool_.server_cq_)
          << "You shouldn't be setting the number of completion queues "
          << "to more than 1 when you're using 'TestingCompletionThreadPool'";
      pool_.server_cq_.emplace(std::move(cq));
    }

    size_t NumberOfCompletionQueues() override {
      return 1;
    }

    stout::borrowed_ref<::grpc::ServerCompletionQueue> Schedule() override {
      CHECK(pool_.server_cq_) << "You haven't added any completion queues yet!";
      return pool_.server_cq_->Borrow();
    }

   private:
    TestingCompletionThreadPool& pool_;
  };

  // Helper for running a single completion queue until idle.
  bool RunUntilIdle(::grpc::CompletionQueue& cq);

  // NOTE: server completion queue is optional because we can't
  // construct one unless you build a server using 'ServerBuilder' and
  // we might have a test that doesn't build a server!
  std::optional<
      stout::Borrowable<
          std::unique_ptr<::grpc::ServerCompletionQueue>>>
      server_cq_;

  stout::Borrowable<::grpc::CompletionQueue> client_cq_;

  // NOTE: invariant here that the server proxy is outlived by 'this'.
  stout::Borrowable<ServerCompletionThreadPoolProxy> server_proxy_;
  stout::Borrowable<ClientCompletionThreadPoolProxy> client_proxy_;

  Semaphore semaphore_;
  std::atomic<bool> pause_ = false;
  std::atomic<bool> paused_ = false;
  std::atomic<bool> shutdown_ = false;
  std::thread thread_;
};

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
