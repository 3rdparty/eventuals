#include "eventuals/grpc/server.h"

#include <thread>

#include "eventuals/catch.h"
#include "eventuals/closure.h"
#include "eventuals/conditional.h"
#include "eventuals/grpc/logging.h"
#include "eventuals/just.h"
#include "eventuals/loop.h"
#include "eventuals/repeat.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/server.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace grpc {

////////////////////////////////////////////////////////////////////////

auto Server::RequestCall(
    ServerContext* context,
    ::grpc::ServerCompletionQueue* cq) {
  return Eventual<void>()
      .context(Callback<bool>())
      // NOTE: 'context' and 'cq' are stored in a 'Closure()' so safe
      // to capture them as references here.
      .start([this, context, cq](auto& callback, auto& k) {
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
            cq,
            cq,
            &callback);
      });
}

////////////////////////////////////////////////////////////////////////

auto Server::Lookup(ServerContext* context) {
  // NOTE: 'context' is stored in a 'Closure()' so safe to capture as
  // a reference here.
  return Synchronized(Then([this, context]() {
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
  return Then([context]() {
    EVENTUALS_GRPC_LOG(1)
        << "Dropping " << context->method()
        << " for host " << context->host();

    auto status = ::grpc::Status(
        ::grpc::UNIMPLEMENTED,
        context->method() + " for host " + context->host());

    context->FinishThenOnDone(status, [context](bool) {
      delete context;
    });
  });
}

////////////////////////////////////////////////////////////////////////

Server::Server(
    std::vector<Service*>&& services,
    std::unique_ptr<::grpc::AsyncGenericService>&& service,
    std::unique_ptr<::grpc::Server>&& server,
    std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>>&& cqs,
    std::vector<std::thread>&& threads)
  : service_(std::move(service)),
    server_(std::move(server)),
    cqs_(std::move(cqs)),
    threads_(std::move(threads)) {
  for (auto* service : services) {
    auto& serve = serves_.emplace_back(std::make_unique<Serve>());

    serve->service = service;

    serve->service->Register(this);

    serve->task.emplace(
        Task::Of<void>([service]() {
          // Use a separate preemptible scheduler context to serve
          // each service so that we correctly handle any waiting
          // (e.g., on 'Lock' or 'Wait').
          //
          // TODO(benh): while only one service with the same name
          // should be able to accept at a time we can have one
          // service per host so just using the service name is not
          // unique but we don't have access to host information at
          // this time.
          return Preempt(service->name(), service->Serve());
        }));

    serve->task->Start(
        serve->interrupt,
        [&serve]() {
          EVENTUALS_GRPC_LOG(1)
              << serve->service->name()
              << " completed serving";
          serve->done.store(true);
        },
        [&serve](std::exception_ptr) {
          EVENTUALS_GRPC_LOG(1)
              << serve->service->name()
              << " failed serving";
          serve->done.store(true);
        },
        [&serve]() {
          EVENTUALS_GRPC_LOG(1)
              << serve->service->name()
              << " stopped serving";
          serve->done.store(true);
        });
  }

  workers_.reserve(cqs_.size());

  for (auto&& cq : cqs_) {
    auto& worker = workers_.emplace_back(std::make_unique<Worker>());

    worker->task.emplace(
        cq.get(),
        [this](auto* cq) {
          return Closure(
              [this, cq, context = std::unique_ptr<ServerContext>()]() mutable {
                // Use a separate preemptible scheduler context for
                // each worker so that we correctly handle any waiting
                // (e.g., on 'Lock' or 'Wait').
                return Preempt(
                    "[" + std::to_string((size_t) cq) + "]",
                    Repeat([&]() mutable {
                      context = std::make_unique<ServerContext>();
                      return RequestCall(context.get(), cq)
                          | Lookup(context.get())
                          | Conditional(
                                 [](auto* endpoint) {
                                   return endpoint != nullptr;
                                 },
                                 [&](auto* endpoint) {
                                   return endpoint->Enqueue(
                                       std::move(context));
                                 },
                                 [&](auto*) {
                                   return Unimplemented(context.release());
                                 });
                    })
                        | Loop()
                        | Catch([this](auto&&...) {
                            // TODO(benh): refactor so we only call
                            // 'ShutdownEndpoints()' once on server
                            // shutdown, not for each worker (which
                            // should be harmless but unnecessary).
                            return ShutdownEndpoints();
                          }));
              });
        });

    worker->task->Start(
        worker->interrupt,
        [&worker]() {
          worker->done.store(true);
        },
        [](std::exception_ptr) {
          LOG(FATAL) << "Unreachable";
        },
        []() {
          LOG(FATAL) << "Unreachable";
        });
  }
}

////////////////////////////////////////////////////////////////////////

Server::~Server() {
  // NOTE: unlike gRPC we try 'Shutdown()' and 'Wait()' here during
  // destruction so that resources are properly released. While this
  // is safer, it is different than gRPCs semantics hence being called
  // out explicitly here!
  Shutdown();
  Wait();
}

////////////////////////////////////////////////////////////////////////

void Server::Shutdown() {
  // Server might have already been shutdown.
  if (server_) {
    server_->Shutdown();
  }

  // NOTE: we don't interrupt 'workers_' or 'serves_' as shutting down
  // the server should force each worker waiting on 'RequestCall()' to
  // get an error which should then cause 'ShutdownEndpoints()' which
  // should propagate through to each 'serve_' that might have been
  // waiting for the next accepted call.
}

////////////////////////////////////////////////////////////////////////

void Server::Wait() {
  if (server_) {
    // We first wait for the underlying server to shutdown, that means
    // that all the workers and the serves should have gotten some
    // kind of error and be shutting down themselves.
    server_->Wait();

    // Now wait for the workers to complete.
    for (auto& worker : workers_) {
      while (!worker->done.load()) {
        // TODO(benh): cpu relax or some other spin loop strategy.
      }
    }

    // Now wait for the serve tasks to be done (note that like workers
    // ordering is not important since these are each independent).
    for (auto& serve : serves_) {
      while (!serve->done.load()) {
        // TODO(benh): cpu relax or some other spin loop strategy.
      }
    }

    // We shutdown the completion queues _after_ all 'workers_' and
    // 'serves_' have completed because if they try to use the
    // completion queues after they're shutdown that may cause
    // internal grpc assertions to fire (which makes sense, we called
    // shutdown on them and then tried to use them).
    //
    // NOTE: it's unclear if we need to shutdown the completion queues
    // before we wait on the server but at least emperically it
    // appears that it doesn't matter.
    for (auto& cq : cqs_) {
      cq->Shutdown();
    }

    for (auto& thread : threads_) {
      thread.join();
    }

    for (auto& cq : cqs_) {
      void* tag = nullptr;
      bool ok = false;
      while (cq->Next(&tag, &ok)) {}
    }

    // NOTE: gRPC doesn't want us calling 'Wait()' more than once (as
    // in, it causes an abort) presumably because it has already
    // released resources. This is possible at the very least if one
    // manually calls this function and then this function gets called
    // again from the destructor. Thus, we reset 'server_' here (BUT
    // AFTER WE HAVE WAITED FOR ALL THREADS ABOVE TO HAVE JOINED) so
    // that we won't try and call 'Wait()' more than once because
    // 'server_' will be valueless (or call 'Shutdown()' more than
    // once since we also check for 'server_' there).
    server_.reset();
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

ServerBuilder& ServerBuilder::RegisterService(Service* service) {
  services_.push_back(service);
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

  auto service = std::make_unique<::grpc::AsyncGenericService>();

  builder_.RegisterAsyncGenericService(service.get());

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
    return ServerStatusOrServer{
        // TODO(benh): Are invalid addresses the only reason the
        // server wouldn't start? What about bad credentials?
        ServerStatus::Error("Error building server: invalid address(es)"),
        nullptr};
  } else {
    // NOTE: we wait to start the threads until after a succesful
    // 'BuildAndStart()' so that we don't have to bother with
    // stopping/joining.
    std::vector<std::thread> threads;
    for (auto& cq : cqs) {
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
        // NOTE: using 'new' here because constructor for 'Server' is
        // private and can't be invoked by 'std::make_unique()'.
        std::unique_ptr<Server>(new Server(
            std::move(services_),
            std::move(service),
            std::move(server),
            std::move(cqs),
            std::move(threads)))};
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
