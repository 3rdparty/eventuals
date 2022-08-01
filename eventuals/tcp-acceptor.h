#pragma once

#include <mutex>
#include <optional>

#include "tcp-base.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {

////////////////////////////////////////////////////////////////////////

class Acceptor final {
 public:
  explicit Acceptor(Protocol protocol, EventLoop& loop = EventLoop::Default())
    : loop_(loop),
      protocol_(protocol),
      acceptor_(loop.io_context()) {
    // NOTE: Compiling this class produces
    // 'unused-private-field' warnings, hence why this
    // piece of code is needed.
    (void) is_listening_;
    (void) protocol_;
  }

  ~Acceptor() {
    CHECK(!IsOpen()) << "Close the acceptor before destructing";
  }

  Acceptor(const Acceptor& that) = delete;
  // TODO(folming): implement move.
  Acceptor(Acceptor&& that) = delete;

  Acceptor& operator=(const Acceptor& that) = delete;
  // TODO(folming): implement move.
  Acceptor& operator=(Acceptor&& that) = delete;

  [[nodiscard]] auto Open();

  [[nodiscard]] auto Bind(std::string&& ip, uint16_t port);

  [[nodiscard]] auto Listen(int backlog);

  [[nodiscard]] auto Accept(SocketBase& socket);

  [[nodiscard]] auto Close();

  bool IsOpen() {
    return is_open_.load();
  }

  uint16_t ListeningPort() {
    auto port_optional = port_.load();
    CHECK(port_optional.has_value()) << "Listen must be called beforehand";
    return *port_optional;
  }

  std::string ListeningIP() {
    std::lock_guard<std::mutex> lk(ip_mutex_);
    CHECK(ip_.has_value()) << "Listen must be called beforehand";
    return *ip_;
  }

 private:
  asio::ip::tcp::acceptor& acceptor_handle() {
    return acceptor_;
  }

  asio::io_context& io_context() {
    return loop_.io_context();
  }

  EventLoop& loop_;
  // asio::ip::tcp::acceptor's methods are not thread-safe,
  // so we store the state in an atomic variables by ourselves.
  std::atomic<bool> is_open_ = false;
  std::atomic<std::optional<uint16_t>> port_;

  // std::string type is not trivially copyable,
  // so we can't use std::atomic here.
  // Instead, we use std::mutex to access and modify this variable.
  std::mutex ip_mutex_;
  std::optional<std::string> ip_;

  // This variable is only accessed or modified inside event loop,
  // so we don't need std::atomic wrapper.
  bool is_listening_ = false;

  Protocol protocol_;

  asio::ip::tcp::acceptor acceptor_;
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Acceptor::Open() {
  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(this)
          .start([](Acceptor*& acceptor,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            asio::post(
                acceptor->io_context(),
                [&]() {
                  if (handler.has_value()) {
                    if (handler->interrupt().Triggered()) {
                      k.Stop();
                      return;
                    }
                  }

                  if (acceptor->IsOpen()) {
                    k.Fail(std::runtime_error("Acceptor is already opened"));
                    return;
                  }

                  asio::error_code error;

                  switch (acceptor->protocol_) {
                    case Protocol::IPV4:
                      acceptor->acceptor_handle().open(
                          asio::ip::tcp::v4(),
                          error);
                      break;
                    case Protocol::IPV6:
                      acceptor->acceptor_handle().open(
                          asio::ip::tcp::v6(),
                          error);
                      break;
                  }

                  if (!error) {
                    acceptor->is_open_.store(true);
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Acceptor::Bind(std::string&& ip, uint16_t port) {
  struct Context {
    Acceptor* acceptor;
    std::string ip;
    uint16_t port;
  };

  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Context{this, std::move(ip), port})
          .start([](Context& context,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            asio::post(
                context.acceptor->io_context(),
                [&]() {
                  if (handler.has_value()) {
                    if (handler->interrupt().Triggered()) {
                      k.Stop();
                      return;
                    }
                  }

                  if (!context.acceptor->IsOpen()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  if (context.acceptor->is_listening_) {
                    k.Fail(
                        std::runtime_error(
                            "Bind call is forbidden "
                            "while acceptor is listening"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  switch (context.acceptor->protocol_) {
                    case Protocol::IPV4:
                      endpoint = asio::ip::tcp::endpoint(
                          asio::ip::make_address_v4(context.ip, error),
                          context.port);
                      break;
                    case Protocol::IPV6:
                      endpoint = asio::ip::tcp::endpoint(
                          asio::ip::make_address_v6(context.ip, error),
                          context.port);
                      break;
                  }


                  if (error) {
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  context.acceptor->acceptor_handle().bind(endpoint, error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Acceptor::Listen(const int backlog) {
  struct Context {
    Acceptor* acceptor;
    const int backlog;
  };

  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Context{this, backlog})
          .start([](Context& context,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            asio::post(
                context.acceptor->io_context(),
                [&]() {
                  if (handler.has_value()) {
                    if (handler->interrupt().Triggered()) {
                      k.Stop();
                      return;
                    }
                  }

                  if (!context.acceptor->IsOpen()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  if (context.acceptor->is_listening_) {
                    k.Fail(
                        std::runtime_error(
                            "Acceptor is already listening"));
                    return;
                  }

                  asio::error_code error;

                  context.acceptor->acceptor_handle().listen(
                      context.backlog,
                      error);

                  if (!error) {
                    context.acceptor->is_listening_ = true;
                    context.acceptor->port_.store(
                        context.acceptor->acceptor_handle()
                            .local_endpoint()
                            .port());
                    std::lock_guard<std::mutex> lk(context.acceptor->ip_mutex_);
                    context.acceptor->ip_ = context.acceptor->acceptor_handle()
                                                .local_endpoint()
                                                .address()
                                                .to_string();
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Acceptor::Accept(SocketBase& socket) {
  struct Context {
    Acceptor* acceptor;
    SocketBase* socket;

    // Used for interrupt handler due to
    // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
    // requirement for handler.Install().
    void* k = nullptr;

    bool started = false;
    bool completed = false;
  };

  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Context{this, &socket})
          .start([](Context& context,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            using K = std::decay_t<decltype(k)>;
            context.k = &k;

            if (handler.has_value()) {
              handler->Install([&context]() {
                asio::post(context.acceptor->io_context(), [&]() {
                  K& k = *static_cast<K*>(context.k);

                  if (!context.started) {
                    context.completed = true;
                    k.Stop();
                    return;
                  } else if (!context.completed) {
                    context.completed = true;
                    asio::error_code error;
                    context.acceptor->acceptor_handle().cancel(error);

                    if (!error) {
                      k.Stop();
                    } else {
                      k.Fail(std::runtime_error(error.message()));
                    }
                  }
                });
              });
            }

            asio::post(
                context.acceptor->io_context(),
                [&]() {
                  if (!context.completed) {
                    if (handler.has_value()) {
                      if (handler->interrupt().Triggered()) {
                        context.completed = true;
                        k.Stop();
                        return;
                      }
                    }

                    CHECK(!context.started);
                    context.started = true;

                    if (!context.acceptor->IsOpen()) {
                      context.completed = true;
                      k.Fail(std::runtime_error("Acceptor is closed"));
                      return;
                    }

                    if (!context.acceptor->is_listening_) {
                      context.completed = true;
                      k.Fail(std::runtime_error("Acceptor is not listening"));
                      return;
                    }

                    if (context.socket->IsOpen()) {
                      context.completed = true;
                      k.Fail(
                          std::runtime_error(
                              "Passed socket is not closed"));
                      return;
                    }

                    if (context.acceptor->protocol_
                        != context.socket->protocol_) {
                      context.completed = true;
                      k.Fail(
                          std::runtime_error(
                              "Passed socket's protocol "
                              "is different from acceptor's"));
                      return;
                    }

                    context.acceptor->acceptor_handle().async_accept(
                        context.socket->socket_handle(),
                        [&](const asio::error_code& error) {
                          if (!context.completed) {
                            context.completed = true;

                            if (!error) {
                              context.socket->is_open_.store(true);
                              context.socket->is_connected_ = true;
                              k.Start();
                            } else {
                              k.Fail(std::runtime_error(error.message()));
                            }
                          }
                        });
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Acceptor::Close() {
  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(this)
          .start([](Acceptor*& acceptor,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            asio::post(
                acceptor->io_context(),
                [&]() {
                  if (handler.has_value()) {
                    if (handler->interrupt().Triggered()) {
                      k.Stop();
                      return;
                    }
                  }

                  if (!acceptor->IsOpen()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  asio::error_code error;
                  acceptor->acceptor_handle().close(error);

                  if (!error) {
                    acceptor->is_open_.store(false);
                    acceptor->is_listening_ = false;
                    acceptor->port_.store(std::nullopt);
                    std::lock_guard<std::mutex> lk(acceptor->ip_mutex_);
                    acceptor->ip_ = std::nullopt;
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

} // namespace tcp
} // namespace ip
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
