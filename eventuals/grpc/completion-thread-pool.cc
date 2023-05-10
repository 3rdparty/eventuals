#include "eventuals/grpc/completion-thread-pool.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

TestingCompletionThreadPool::TestingCompletionThreadPool()
  : server_proxy_(*this),
    client_proxy_(client_cq_.Borrow()),
    thread_(
        [this]() {
          do {
            while (!pause_ && !shutdown_) {
              RunUntilIdle();
            }
            if (!shutdown_) {
              paused_ = true;
              semaphore_.Wait();
              paused_ = false;
            }
          } while (!shutdown_);
        },
        "grpc comp. q.") {}

////////////////////////////////////////////////////////////////////////

TestingCompletionThreadPool::~TestingCompletionThreadPool() {
  shutdown_ = true;
  semaphore_.Signal();
  thread_.join();

  void* tag = nullptr;
  bool ok = false;

  client_cq_->Shutdown();
  while (client_cq_->Next(&tag, &ok)) {}

  server_cq_->get()->Shutdown();
  while (server_cq_->get()->Next(&tag, &ok)) {}
}

////////////////////////////////////////////////////////////////////////

void TestingCompletionThreadPool::Pause() {
  pause_ = true;
  while (!paused_) {}
}

////////////////////////////////////////////////////////////////////////

void TestingCompletionThreadPool::Resume() {
  pause_ = false;
  semaphore_.Signal();
}

////////////////////////////////////////////////////////////////////////

stout::borrowed_ref<CompletionThreadPool<::grpc::CompletionQueue>>
TestingCompletionThreadPool::ClientCompletionThreadPool() {
  return client_proxy_.Borrow();
}

////////////////////////////////////////////////////////////////////////

stout::borrowed_ref<CompletionThreadPool<::grpc::ServerCompletionQueue>>
TestingCompletionThreadPool::ServerCompletionThreadPool() {
  return server_proxy_.Borrow();
}

////////////////////////////////////////////////////////////////////////

bool TestingCompletionThreadPool::RunUntilIdle() {
  if (!thread_.is_current_thread()) {
    CHECK(paused_) << "need to 'Pause()' the thread pool first!";
  }

  bool events = false;
  bool server_got_events = true;
  bool client_got_events = true;

  do {
    if (server_cq_) {
      server_got_events = RunUntilIdle(*(server_cq_->get()));
    } else {
      server_got_events = false;
    }

    client_got_events = RunUntilIdle(*client_cq_);

    if (client_got_events || server_got_events) {
      events = true;
    }
  } while (client_got_events || server_got_events);

  return events;
}

////////////////////////////////////////////////////////////////////////

bool TestingCompletionThreadPool::RunUntilIdle(::grpc::CompletionQueue& cq) {
  bool events = false;
  bool timeout = false;
  do {
    void* tag = nullptr;
    bool ok = false;

    gpr_timespec deadline;
    deadline.clock_type = GPR_TIMESPAN;
    deadline.tv_sec = 0;
    deadline.tv_nsec = 0;

    switch (cq.AsyncNext(&tag, &ok, deadline)) {
      case ::grpc::CompletionQueue::SHUTDOWN:
        LOG(FATAL) << "Running the completion queue after shutting it down!";
      case ::grpc::CompletionQueue::GOT_EVENT:
        events = true;
        (*static_cast<Callback<void(bool)>*>(tag))(ok);
        break;
      case ::grpc::CompletionQueue::TIMEOUT:
        timeout = true;
        break;
    }
  } while (!timeout);
  return events;
}

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
