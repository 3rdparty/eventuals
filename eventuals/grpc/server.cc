#include "eventuals/grpc/server.hh"

#include <thread>

#include "eventuals/catch.hh"
#include "eventuals/closure.hh"
#include "eventuals/conditional.hh"
#include "eventuals/grpc/logging.hh"
#include "eventuals/just.hh"
#include "eventuals/loop.hh"
#include "eventuals/repeat.hh"
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
      .raises<std::runtime_error>()
      .context(Callback<void(bool)>())
      // NOTE: 'context' and 'cq' are stored in a 'Closure()' so safe
      // to capture them as references here.
      .start([this, context, cq](Callback<void(bool)>& callback, auto& k) {
        if (!callback) {
          callback = [&k](bool ok) {
            if (ok) {
              k.Start();
            } else {
              k.Fail(std::runtime_error("RequestCall !ok"));
            }
          };
        }

        service_->RequestCall(
            context->context(),
            context->stream(),
            // TODO(benh): use completion queue from
            // CompletionThreadPool for each call rather than the
            // notification completion queue that we are using for
            // server notifications?
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
        << "Dropping call for host " << context->host()
        << " and path = " << context->method();

    ::grpc::Status status(
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
    std::variant<
        stout::borrowed_ref<
            CompletionThreadPool<
                ::grpc::ServerCompletionQueue>>,
        std::unique_ptr<
            CompletionThreadPool<
                ::grpc::ServerCompletionQueue>>>&& pool)
  : pool_(std::move(pool)),
    service_(std::move(service)),
    server_(std::move(server)) {
  for (Service* service : services) {
    auto& serve = serves_.emplace_back(std::make_unique<Serve>());

    serve->service = service;

    serve->service->Register(this);

    serve->task.emplace(
        Task::Of<void>::Raises<std::exception>([service]() {
          return service->Serve();
        }));

    serve->task->Start(
        // TODO(benh): while only one service with the same name
        // should be able to accept at a time we can have one service
        // per host so just using the service name is not unique but
        // we don't have access to host information at this time.
        serve->service->name(),
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

  workers_.reserve(this->pool().NumberOfCompletionQueues());

  for (size_t i = 0; i < this->pool().NumberOfCompletionQueues(); ++i) {
    auto& worker = workers_.emplace_back(std::make_unique<Worker>());

    // NOTE: we're currently relying on the fact that a
    // 'StaticCompletionThreadPool' will "schedule" completion queues
    // in a "least loaded" way which will ensure that we have at least
    // one worker per completion queue below which is imperative
    // otherwise we may fail to accept RPCs!
    stout::borrowed_ref<::grpc::ServerCompletionQueue> cq =
        this->pool().Schedule();

    worker->task.emplace(
        cq.reborrow(),
        [this](stout::borrowed_ref<::grpc::ServerCompletionQueue>& cq) {
          return Closure(
              [this,
               &cq,
               context = std::unique_ptr<ServerContext>()]() mutable {
                return Repeat([&]() mutable {
                         context = std::make_unique<ServerContext>();
                         return RequestCall(context.get(), cq.get())
                             >> Lookup(context.get())
                             >> Conditional(
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
                    >> Loop()
                    >> Catch()
                           .raised<std::exception>(
                               [this](std::exception&& e) {
                                 EVENTUALS_GRPC_LOG(1)
                                     << "Failed to accept a call: "
                                     << e.what() << "; shutting down";

                                 // TODO(benh): refactor so we only call
                                 // 'ShutdownEndpoints()' once on server
                                 // shutdown, not for each worker (which
                                 // should be harmless but unnecessary).
                                 return ShutdownEndpoints();
                               });
              });
        });

    worker->task->Start(
        "[worker for queue " + std::to_string((size_t) cq.get()) + "]",
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

void Server::Shutdown(
    const std::optional<
        std::chrono::time_point<
            std::chrono::system_clock>>& deadline) {
  // Server might have already been shutdown.
  if (server_) {
    if (deadline.has_value()) {
      server_->Shutdown(deadline.value());
    } else {
      server_->Shutdown();
    }
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
    for (std::unique_ptr<Worker>& worker : workers_) {
      while (!worker->done.load()) {
        // TODO(benh): cpu relax or some other spin loop strategy.
      }
    }

    // Now wait for the serve tasks to be done (note that like workers
    // ordering is not important since these are each independent).
    for (std::unique_ptr<Serve>& serve : serves_) {
      while (!serve->done.load()) {
        // TODO(benh): cpu relax or some other spin loop strategy.
      }
    }

    // We can't shutdown the completion thread pool until _after_ all
    // 'workers_' and 'serves_' have completed because if they try to
    // use the completion queues after they're shutdown that may cause
    // internal grpc assertions to fire (which makes sense, we called
    // shutdown on them and then tried to use them).
    //
    // TODO(benh): technically some of the threads in the completion
    // thread pool might still be executing _eventuals_ returned from
    // calling 'Server' functions (e.g., 'Server::Lookup()') but since
    // all of those eventuals have completed (since we waited for
    // 'workers_' and 'serves_' above) they should just be unwinding
    // their stack and not read or write any memory associated with
    // 'this'. For better safety we might instead want to revisit the
    // 'CompletionThreadPool' interface to allow us to 'Shutdown()'
    // and 'Wait()' for it. This is a little tricky because a thread
    // pool might be used for (multiple) servers and (multiple)
    // clients so really we just want to shutdown the threads that are
    // currently executing from completion queues associated with this
    // server.

    // NOTE: gRPC doesn't want us calling 'Wait()' more than once (as
    // in, it causes an abort) presumably because it has already
    // released resources. This is possible at the very least if one
    // manually calls this function and then this function gets called
    // again from the destructor. Thus, we reset 'server_' here (BUT
    // AFTER WE HAVE WAITED FOR ANYTHING THAT WOULD HAVE USED
    // 'server_' ABOVE) so that we won't try and call 'Wait()' more
    // than once because 'server_' will be valueless (or call
    // 'Shutdown()' more than once since we also check for 'server_'
    // there).
    server_.reset();
  }
}

////////////////////////////////////////////////////////////////////////

CompletionThreadPool<::grpc::ServerCompletionQueue>& Server::pool() {
  return std::visit(
      [](auto& pool) -> CompletionThreadPool<::grpc::ServerCompletionQueue>& {
        return *pool;
      },
      pool_);
}

////////////////////////////////////////////////////////////////////////

ServerBuilder& ServerBuilder::SetCompletionThreadPool(
    stout::borrowed_ref<
        CompletionThreadPool<::grpc::ServerCompletionQueue>>&& pool) {
  if (completion_thread_pool_) {
    std::string error = "already set completion thread pool";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    completion_thread_pool_.emplace(std::move(pool));
  }
  return *this;
}

////////////////////////////////////////////////////////////////////////

ServerBuilder& ServerBuilder::SetNumberOfCompletionQueues(size_t n) {
  if (number_of_completion_queues_) {
    std::string error = "already set number of completion queues";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    number_of_completion_queues_ = n;
  }
  return *this;
}

////////////////////////////////////////////////////////////////////////

// TODO(benh): Provide a 'setMaximumThreadsPerCompletionQueue' as well.
ServerBuilder& ServerBuilder::SetMinimumThreadsPerCompletionQueue(size_t n) {
  if (minimum_threads_per_completion_queue_) {
    std::string error = "already set minimum threads per completion queue";
    if (!status_.ok()) {
      status_ = ServerStatus::Error(status_.error() + "; " + error);
    } else {
      status_ = ServerStatus::Error(error);
    }
  } else {
    minimum_threads_per_completion_queue_ = n;
  }
  return *this;
}

////////////////////////////////////////////////////////////////////////

ServerBuilder& ServerBuilder::SetMaxReceiveMessageSize(
    const int max_receive_message_size) {
  builder_.SetMaxReceiveMessageSize(max_receive_message_size);
  return *this;
}

////////////////////////////////////////////////////////////////////////

ServerBuilder& ServerBuilder::SetMaxSendMessageSize(
    const int max_receive_message_size) {
  builder_.SetMaxSendMessageSize(max_receive_message_size);
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

  if (!number_of_completion_queues_) {
    if (!completion_thread_pool_) {
      number_of_completion_queues_ = std::thread::hardware_concurrency();
    } else {
      return ServerStatusOrServer{
          ServerStatus::Error(
              "if you 'SetCompletionThreadPool()' you must also "
              "'SetNumberOfCompletionQueues()'"),
          nullptr};
    }
  }

  if (completion_thread_pool_ && minimum_threads_per_completion_queue_) {
    return ServerStatusOrServer{
        ServerStatus::Error(
            "you can't 'SetCompletionThreadPool()' and "
            "'SetMinimumThreadsPerCompletionQueue()'"),
        nullptr};
  }

  std::vector<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs;

  cqs.reserve(number_of_completion_queues_.value());

  for (size_t i = 0; i < number_of_completion_queues_.value(); ++i) {
    cqs.emplace_back(builder_.AddCompletionQueue());
  }

  std::unique_ptr<::grpc::Server> server = builder_.BuildAndStart();

  if (!server) {
    return ServerStatusOrServer{
        // TODO(benh): Are invalid addresses the only reason the
        // server wouldn't start? What about bad credentials?
        ServerStatus::Error("Error building server: invalid address(es)"),
        nullptr};
  } else {
    // NOTE: we wait to create the completion thread pool until after
    // a successful 'BuildAndStart()' so that we don't have to bother
    // with starting and the possibley stopping/joining threads.
    auto pool = [&]() -> BorrowedOrOwnedCompletionThreadPool {
      if (completion_thread_pool_) {
        for (std::unique_ptr<::grpc::ServerCompletionQueue>& cq : cqs) {
          completion_thread_pool_.value()->AddCompletionQueue(std::move(cq));
        }
        return std::move(completion_thread_pool_.value());
      } else {
        return std::make_unique<ServerCompletionThreadPool>(
            std::move(cqs),
            minimum_threads_per_completion_queue_.value_or(1));
      }
    }();

    return ServerStatusOrServer{
        ServerStatus::Ok(),
        // NOTE: using 'new' here because constructor for 'Server' is
        // private and can't be invoked by 'std::make_unique()'.
        std::unique_ptr<Server>(new Server(
            std::move(services_),
            std::move(service),
            std::move(server),
            std::move(pool)))};
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
