#pragma once

#include <cassert>
#include <thread>

#include "eventuals/callback.h"
#include "grpcpp/completion_queue.h"
#include "stout/borrowable.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

class CompletionPool {
 public:
  CompletionPool() {
    unsigned int threads = std::thread::hardware_concurrency();
    threads_.reserve(threads);
    cqs_.reserve(threads);
    for (size_t i = 0; i < threads; i++) {
      cqs_.emplace_back(new stout::Borrowable<::grpc::CompletionQueue>());
      threads_.emplace_back(
          [cq = cqs_.back()->get()]() {
            void* tag = nullptr;
            bool ok = false;
            while (cq->Next(&tag, &ok)) {
              (*static_cast<Callback<void(bool)>*>(tag))(ok);
            }
          });
    }
  }

  ~CompletionPool() {
    Shutdown();
    Wait();
  }

  void Shutdown() {
    if (!shutdown_) {
      for (auto& cq : cqs_) {
        cq->get()->Shutdown();
      }
      shutdown_ = true;
    }
  }

  void Wait() {
    while (!threads_.empty()) {
      auto& thread = threads_.back();

      thread.join();

      threads_.pop_back();

      auto& cq = cqs_.back();

      void* tag = nullptr;
      bool ok = false;
      while (cq->get()->Next(&tag, &ok)) {}

      cqs_.pop_back();
    }
  }

  stout::borrowed_ptr<::grpc::CompletionQueue> Schedule() {
    // TODO(benh): provide alternative "scheduling" algorithms in
    // addition to "least loaded", e.g., round-robin, random.
    stout::Borrowable<::grpc::CompletionQueue>* selected = nullptr;
    size_t load = SIZE_MAX;
    for (auto& cq : cqs_) {
      auto borrows = cq->borrows();
      if (borrows < load) {
        selected = cq.get();
        load = borrows;
      }
    }
    CHECK(selected != nullptr);
    return selected->Borrow();
  }

 private:
  std::vector<std::unique_ptr<stout::Borrowable<::grpc::CompletionQueue>>> cqs_;

  std::vector<std::thread> threads_;

  bool shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
