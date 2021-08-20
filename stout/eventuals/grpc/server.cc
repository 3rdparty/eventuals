#include "stout/eventuals/grpc/server.h"

#include <thread>

#include "grpcpp/completion_queue.h"
#include "grpcpp/server.h"
#include "stout/closure.h"
#include "stout/conditional.h"
#include "stout/eventuals/grpc/server.h"
#include "stout/grpc/logging.h"
#include "stout/just.h"
#include "stout/loop.h"
#include "stout/repeat.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

auto Server::RequestCall(
    std::unique_ptr<ServerContext>& context,
    ::grpc::ServerCompletionQueue& cq) {
  return Eventual<void>()
      .context(Callback<bool>())
      // NOTE: 'context' and 'cq' are stored in a 'Closure()' so safe
      // to capture them as references here.
      .start([&](auto& callback, auto& k) {
        if (!callback) {
          callback = [&k](bool ok) {
            if (ok) {
              k.Start();
            } else {
              k.Fail(ServerStatus::Error("RequestCall !ok"));
            }
          };
        }

        service_->RequestCall(
            context->context(),
            context->stream(),
            // TODO(benh): use completion queue from
            // CompletionPool for each call rather than the
            // notification completion queue that we are using
            // for server notifications?
            &cq,
            &cq,
            &callback);
      });
}

////////////////////////////////////////////////////////////////////////

auto Server::Lookup(std::unique_ptr<ServerContext>& context) {
  // NOTE: 'context' is stored in a 'Closure()' so safe to capture as
  // a reference here.
  return Synchronized(Lambda([&]() {
    Endpoint* endpoint = nullptr;

    auto iterator = endpoints_.find(
        std::make_pair(context->method(), context->host()));

    if (iterator != endpoints_.end()) {
      endpoint = iterator->second.get();
    } else {
      iterator = endpoints_.find(
          std::make_pair(context->method(), "*"));

      if (iterator != endpoints_.end()) {
        endpoint = iterator->second.get();
      }
    }

    return endpoint;
  }));
}

////////////////////////////////////////////////////////////////////////

auto Server::Unimplemented(ServerContext* context) {
  return Lambda([context]() {
    STOUT_GRPC_LOG(1)
        << "Dropping " << context->method()
        << " for host " << context->host();

    auto status = ::grpc::Status(
        ::grpc::UNIMPLEMENTED,
        context->method() + " for host " + context->host());

    static Callback<bool> noop = [](bool) {};

    context->stream()->Finish(status, &noop);

    context->OnDone([context](bool) {
      delete context;
    });
  });
}

////////////////////////////////////////////////////////////////////////

Server::Server(
    std::unique_ptr<::grpc::AsyncGenericService>&& service,
    std::unique_ptr<::grpc::Server>&& server,
    std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>>&& cqs,
    std::vector<std::thread>&& threads)
  : Synchronizable(&lock_),
    service_(std::move(service)),
    server_(std::move(server)),
    cqs_(std::move(cqs)),
    threads_(std::move(threads)) {
  workers_.reserve(cqs_.size());

  for (auto&& cq : cqs_) {
    auto& worker = workers_.emplace_back(std::make_unique<Worker>());

    worker->task.emplace(
        cq.get(),
        [this](auto* cq) {
          return Closure(
              [this, cq, context = std::unique_ptr<ServerContext>()]() mutable {
                return Repeat(
                           Lambda([&]() mutable {
                             context = std::make_unique<ServerContext>();
                           })
                           | RequestCall(context, *cq)
                           | Lookup(context)
                           | Conditional(
                               [](auto* endpoint) {
                                 return endpoint != nullptr;
                               },
                               [&](auto* endpoint) {
                                 return endpoint->Enqueue(std::move(context));
                               },
                               [&](auto*) {
                                 return Unimplemented(context.release());
                               }))
                    | Loop()
                    | Just(Undefined()); // TODO(benh): render this unnecessary.
              });
        });

    worker->task->Start(
        worker->interrupt,
        [](auto&&...) {
          LOG(FATAL) << "Unreachable";
        },
        [&worker](std::exception_ptr) {
          worker->done.store(true);
        },
        []() {
          LOG(FATAL) << "Unreachable";
        });
  }
}

////////////////////////////////////////////////////////////////////////

Server::~Server() {
  Shutdown();
  Wait();
}

////////////////////////////////////////////////////////////////////////

void Server::Shutdown() {
  // Server might have gotten moved.
  if (server_) {
    server_->Shutdown();
  }

  for (auto&& cq : cqs_) {
    cq->Shutdown();
  }
}

////////////////////////////////////////////////////////////////////////

void Server::Wait() {
  if (server_) {
    server_->Wait();
  }

  for (auto&& thread : threads_) {
    thread.join();
  }

  for (auto&& cq : cqs_) {
    void* tag = nullptr;
    bool ok = false;
    while (cq->Next(&tag, &ok)) {}
  }

  for (auto& worker : workers_) {
    while (!worker->done.load()) {}
  }
}

////////////////////////////////////////////////////////////////////////

ServerBuilder& ServerBuilder::SetNumberOfCompletionQueues(size_t n) {
  if (numberOfCompletionQueues_) {
    std::string error = "already set number of completion queues";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    numberOfCompletionQueues_ = n;
  }
  return *this;
}

////////////////////////////////////////////////////////////////////////

// TODO(benh): Provide a 'setMaximumThreadsPerCompletionQueue' as well.
ServerBuilder& ServerBuilder::SetMinimumThreadsPerCompletionQueue(size_t n) {
  if (minimumThreadsPerCompletionQueue_) {
    std::string error = "already set minimum threads per completion queue";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    minimumThreadsPerCompletionQueue_ = n;
  }
  return *this;
}

////////////////////////////////////////////////////////////////////////

ServerBuilder& ServerBuilder::AddListeningPort(
    const std::string& address,
    std::shared_ptr<::grpc::ServerCredentials> credentials,
    int* selectedPort) {
  addresses_.push_back(address);
  builder_.AddListeningPort(address, credentials, selectedPort);
  return *this;
}

////////////////////////////////////////////////////////////////////////

ServerStatusOrServer ServerBuilder::BuildAndStart() {
  if (addresses_.empty()) {
    const std::string error = "no listening addresses specified";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  }

  if (!status_.ok()) {
    return ServerStatusOrServer{
        ServerStatus::Error("Error building server: " + status_.error()),
        nullptr};
  }

  service_ = absl::make_unique<::grpc::AsyncGenericService>();

  builder_.RegisterAsyncGenericService(service_.get());

  if (!numberOfCompletionQueues_) {
    numberOfCompletionQueues_ = 1;
  }

  if (!minimumThreadsPerCompletionQueue_) {
    minimumThreadsPerCompletionQueue_ = 1;
  }

  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs;

  for (size_t i = 0; i < numberOfCompletionQueues_.value(); ++i) {
    cqs.push_back(builder_.AddCompletionQueue());
  }

  std::unique_ptr<::grpc::Server> server = builder_.BuildAndStart();

  if (!server) {
    // TODO(benh): Are invalid addresses the only reason the server
    // wouldn't start? What about bad credentials?
    status_ = ServerStatus::Error("Error building server: invalid address(es)");

    return ServerStatusOrServer{
        status_,
        nullptr};
  } else {
    // NOTE: we wait to start the threads until after a succesful
    // 'BuildAndStart()' so that we don't have to bother with
    // stopping/joining.
    std::vector<std::thread> threads;
    for (auto&& cq : cqs) {
      for (size_t j = 0; j < minimumThreadsPerCompletionQueue_.value(); ++j) {
        threads.push_back(
            std::thread(
                [cq = cq.get()]() {
                  void* tag = nullptr;
                  bool ok = false;
                  while (cq->Next(&tag, &ok)) {
                    (*static_cast<Callback<bool>*>(tag))(ok);
                  }
                }));
      }
    }

    return ServerStatusOrServer{
        ServerStatus::Ok(),
        std::unique_ptr<Server>(new Server(
            std::move(service_),
            std::move(server),
            std::move(cqs),
            std::move(threads)))};
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
