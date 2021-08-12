#pragma once

#include <string>
#include <vector>

#include "stout/borrowable.h"
#include "stout/eventuals/grpc/client.h"
#include "stout/lock.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {
namespace grpc {
namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Broadcast {
  template <typename B_, typename K_>
  struct Adaptor {
    B_& broadcast_;
    K_& k_;

    Lock::Waiter prepare_, ready_, body_, finished_, stop_;

    template <typename F, typename... Args>
    void Serialize(Lock::Waiter* waiter, F&& f, Args&&... args) {
      CHECK(waiter->next == nullptr);

      if (broadcast_.lock_.AcquireFast(waiter)) {
        f(std::forward<Args>(args)...);
        broadcast_.lock_.Release();
      } else {
        // TODO(benh): use an arena or specialized allocator to avoid
        // contention with heap based memory allocation.
        auto* tuple = new std::tuple<Lock*, F, Args...>(
            &broadcast_.lock_,
            std::forward<F>(f),
            std::forward<Args>(args)...);

        waiter->f = [tuple]() mutable {
          std::apply(
              [](auto* lock, auto&& f, auto&&... args) {
                f(std::forward<decltype(args)>(args)...);
                lock->Release();
              },
              std::move(*tuple));

          delete tuple;
        };

        if (broadcast_.lock_.AcquireSlow(waiter)) {
          waiter->f();
        }
      }
    }

    template <typename... Args>
    void Prepare(Args&&... args) {
      Serialize(
          &prepare_,
          [](auto& k, auto&&... args) {
            k.Prepare(std::forward<decltype(args)>(args)...);
          },
          k_,
          std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Ready(Args&&... args) {
      Serialize(
          &ready_,
          [](auto& k, auto& broadcast, auto&&... args) {
            k.Ready(broadcast, std::forward<Args>(args)...);
          },
          k_,
          broadcast_,
          std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      Serialize(
          &body_,
          [](auto& k, auto& broadcast, auto&&... args) {
            k.Body(broadcast, std::forward<Args>(args)...);
          },
          k_,
          broadcast_,
          std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Finished(Args&&... args) {
      Serialize(
          &finished_,
          [](auto& k, auto& broadcast, auto&&... args) {
            k.Finished(broadcast, std::forward<Args>(args)...);
          },
          k_,
          broadcast_,
          std::forward<Args>(args)...);
    }

    void Stop() {
      Serialize(
          &stop_,
          [](auto& broadcast) mutable {
            // TODO(benh): submit to run on the *current* thread pool.
            broadcast.k_.Stop();
            broadcast.lock_.Release();
          },
          broadcast_);
    }
  };

  template <typename K_, typename Request_, typename Response_>
  struct Continuation {
    Continuation(
        K_ k,
        std::string name,
        std::vector<borrowed_ptr<Client>> clients)
      : k_(std::move(k)),
        name_(std::move(name)),
        clients_(std::move(clients)) {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        name_(std::move(that.name_)),
        clients_(std::move(that.clients_)) {}

    template <typename... Args>
    void Start(Args&&...) {
      for (auto& client : clients_) {
        calls_.push_back(
            client->template Call<Request_, Response_>(name_)
                .template k<void>(
                    Adaptor<Continuation, K_>{*this, k_}));
      }

      for (auto& call : calls_) {
        call.Start();
      }

      // NOTE: we install the interrupt handler *after* we've started
      // the calls so as to avoid a race with calling TryCancel() before
      // the call has started.
      if (handler_) {
        if (!handler_->Install()) {
          handler_->Invoke();
        }
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      handler_.emplace(&interrupt, [this]() {
        TryCancel();
      });
    }

    void TryCancel() {
      for (auto& call : calls_) {
        call.context().TryCancel();
      }
    }

    size_t targets() {
      return calls_.size();
    }

    K_ k_;
    std::string name_;
    std::vector<borrowed_ptr<Client>> clients_;

    Lock lock_;

    std::optional<Interrupt::Handler> handler_;

    using Call_ =
        decltype(borrowed_ptr<Client>()
                     ->Call<Request_, Response_>(name_)
                     .template k<void>(
                         std::declval<Adaptor<Continuation, K_>>()));

    std::vector<Call_> calls_;
  };

  template <typename Request_, typename Response_>
  struct Composable {
    using Traits_ = ::stout::grpc::RequestResponseTraits;

    using RequestType_ = typename Traits_::Details<Request_>::Type;
    using ResponseType_ = typename Traits_::Details<Response_>::Type;

    template <typename Arg>
    using ValueFrom = borrowed_ptr<ResponseType_>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Request_, Response_>{
          std::move(k),
          std::move(name_),
          std::move(clients_)};
    }

    std::string name_;
    std::vector<borrowed_ptr<Client>> clients_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

class Cluster {
 public:
  Cluster(
      const std::list<std::string>& targets,
      const std::shared_ptr<::grpc::ChannelCredentials>& credentials,
      borrowable<CompletionPool>& pool) {
    clients_.reserve(targets.size());
    for (const auto& target : targets) {
      clients_.emplace_back(Client(target, credentials, pool.borrow()));
    }
  }

  template <typename Service, typename Request, typename Response>
  auto Broadcast(const std::string& name) {
    static_assert(
        stout::grpc::IsService<Service>::value,
        "expecting \"service\" type to be a protobuf 'Service'");

    return Broadcast<Request, Response>(
        std::string(Service::service_full_name()) + "." + name);
  }

  template <typename Request, typename Response>
  auto Broadcast(const std::string& name) {
    static_assert(
        stout::grpc::IsMessage<Request>::value,
        "expecting \"request\" type to be a protobuf 'Message'");

    static_assert(
        stout::grpc::IsMessage<Response>::value,
        "expecting \"response\" type to be a protobuf 'Message'");

    std::vector<borrowed_ptr<Client>> clients;

    for (auto& client : clients_) {
      clients.push_back(client.borrow());
    }

    return detail::_Broadcast::Composable<Request, Response>{
        name,
        std::move(clients)};
  }

 private:
  std::vector<borrowable<Client>> clients_;
};

////////////////////////////////////////////////////////////////////////

} // namespace grpc
} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
