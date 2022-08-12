#pragma once

#include <deque>
#include <thread>

#include "eventuals/callback.h"
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

  virtual size_t NumberOfCompletionQueues() = 0;

  virtual stout::borrowed_ref<CompletionQueue> Schedule() = 0;
};

////////////////////////////////////////////////////////////////////////

// TODO(benh): 'DynamicCompletionThreadPool' which takes both a
// minimum and a maximum number of polling threads per completion
// queue.

////////////////////////////////////////////////////////////////////////

template <typename CompletionQueue>
class StaticCompletionThreadPool
  : public CompletionThreadPool<CompletionQueue> {
 public:
  StaticCompletionThreadPool(
      std::vector<std::unique_ptr<CompletionQueue>>&& cqs,
      unsigned int number_of_polling_threads_per_completion_queue = 1) {
    threads_.reserve(
        cqs.size() * number_of_polling_threads_per_completion_queue);
    for (std::unique_ptr<CompletionQueue>& cq : cqs) {
      cqs_.emplace_back(std::move(cq));
      for (size_t i = 0;
           i < number_of_polling_threads_per_completion_queue;
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
  }

  StaticCompletionThreadPool(
      unsigned int number_of_completion_queues =
          std::thread::hardware_concurrency(),
      unsigned int number_of_polling_threads_per_completion_queue = 1);

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

  size_t NumberOfCompletionQueues() override {
    return cqs_.size();
  }

  stout::borrowed_ref<CompletionQueue> Schedule() override {
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

  std::vector<std::thread> threads_;

  bool shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

template <>
inline StaticCompletionThreadPool<::grpc::CompletionQueue>::
    StaticCompletionThreadPool(
        unsigned int number_of_completion_queues,
        unsigned int number_of_polling_threads_per_completion_queue)
  : StaticCompletionThreadPool(
      [&number_of_completion_queues]() {
        std::vector<std::unique_ptr<::grpc::CompletionQueue>> cqs;
        for (size_t i = 0; i < number_of_completion_queues; i++) {
          cqs.emplace_back(std::make_unique<::grpc::CompletionQueue>());
        }
        return cqs;
      }(),
      number_of_polling_threads_per_completion_queue) {}

////////////////////////////////////////////////////////////////////////

// '::grpc::ServerCompletionQueue' is not public!
template <>
StaticCompletionThreadPool<::grpc::ServerCompletionQueue>::
    StaticCompletionThreadPool(
        unsigned int number_of_completion_queues,
        unsigned int number_of_polling_threads_per_completion_queue) = delete;

////////////////////////////////////////////////////////////////////////

using ClientCompletionThreadPool =
    StaticCompletionThreadPool<::grpc::CompletionQueue>;

////////////////////////////////////////////////////////////////////////

using ServerCompletionThreadPool =
    StaticCompletionThreadPool<::grpc::ServerCompletionQueue>;

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
