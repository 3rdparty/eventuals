#pragma once

#include "tcp-base.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {

////////////////////////////////////////////////////////////////////////

class Socket final : public SocketBase {
 public:
  explicit Socket(Protocol protocol, EventLoop& loop = EventLoop::Default())
    : SocketBase(protocol, loop),
      socket_(loop.io_context()) {}

  Socket(const Socket& that) = delete;
  // TODO(folming): implement move.
  // It should be possible since asio provides move
  // operations on their socket implementation.
  Socket(Socket&& that) = delete;

  Socket& operator=(const Socket& that) = delete;
  // TODO(folming): implement move.
  // It should be possible since asio provides move
  // operations on their socket implementation.
  Socket& operator=(Socket&& that) = delete;

  ~Socket() override = default;

  [[nodiscard]] auto Receive(
      void* destination,
      size_t destination_size,
      size_t bytes_to_read);

  [[nodiscard]] auto Send(const void* source, size_t source_size);

  [[nodiscard]] auto Close();

 private:
  asio::ip::tcp::socket& socket_handle() override {
    return socket_;
  }

  asio::ip::tcp::socket socket_;
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Socket::Receive(
    void* destination,
    size_t destination_size,
    size_t bytes_to_read) {
  struct Context {
    Socket* socket;
    void* destination;
    size_t destination_size;
    size_t bytes_to_read;

    // Used for interrupt handler due to
    // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
    // requirement for handler.Install().
    void* k = nullptr;

    bool started = false;
    bool completed = false;
  };

  return loop_.Schedule(
      Eventual<size_t>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Context{this, destination, destination_size, bytes_to_read})
          .start([](Context& context,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            using K = std::decay_t<decltype(k)>;
            context.k = &k;

            if (handler.has_value()) {
              handler->Install([&context]() {
                asio::post(context.socket->io_context(), [&]() {
                  K& k = *static_cast<K*>(context.k);

                  if (!context.started) {
                    context.completed = true;
                    k.Stop();
                  } else if (!context.completed) {
                    context.completed = true;
                    asio::error_code error;
                    context.socket->socket_handle().cancel(error);

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
                context.socket->io_context(),
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

                    if (!context.socket->IsOpen()) {
                      context.completed = true;
                      k.Fail(std::runtime_error("Socket is closed"));
                      return;
                    }

                    if (!context.socket->is_connected_) {
                      context.completed = true;
                      k.Fail(std::runtime_error("Socket is not connected"));
                      return;
                    }

                    // Do not allow to read more than destination_size.
                    context.bytes_to_read = std::min(
                        context.bytes_to_read,
                        context.destination_size);

                    // Do not call async_read() if there're 0 bytes to be read.
                    if (context.bytes_to_read == 0) {
                      context.completed = true;
                      k.Start(0);
                      return;
                    }

                    // Start receiving.
                    // Will only succeed after the supplied buffer is full.
                    asio::async_read(
                        context.socket->socket_handle(),
                        asio::buffer(
                            context.destination,
                            context.bytes_to_read),
                        [&](const asio::error_code& error,
                            size_t bytes_transferred) {
                          if (!context.completed) {
                            context.completed = true;

                            if (!error) {
                              k.Start(bytes_transferred);
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

[[nodiscard]] inline auto Socket::Send(
    const void* source,
    size_t source_size) {
  struct Context {
    Socket* socket;
    const void* source;
    size_t source_size;

    // Used for interrupt handler due to
    // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
    // requirement for handler.Install().
    void* k = nullptr;

    bool started = false;
    bool completed = false;
  };

  return loop_.Schedule(
      Eventual<size_t>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Context{this, source, source_size})
          .start([](Context& context,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            using K = std::decay_t<decltype(k)>;
            context.k = &k;

            if (handler.has_value()) {
              handler->Install([&context]() {
                asio::post(context.socket->io_context(), [&]() {
                  K& k = *static_cast<K*>(context.k);

                  if (!context.started) {
                    context.completed = true;
                    k.Stop();
                  } else if (!context.completed) {
                    context.completed = true;
                    asio::error_code error;
                    context.socket->socket_handle().cancel(error);

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
                context.socket->io_context(),
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

                    if (!context.socket->IsOpen()) {
                      context.completed = true;
                      k.Fail(std::runtime_error("Socket is closed"));
                      return;
                    }

                    if (!context.socket->is_connected_) {
                      context.completed = true;
                      k.Fail(std::runtime_error("Socket is not connected"));
                      return;
                    }

                    // Do not call async_write()
                    // if there're 0 bytes to be sent.
                    if (context.source_size == 0) {
                      context.completed = true;
                      k.Start(0);
                      return;
                    }

                    // Will only succeed after
                    // writing all of the data to socket.
                    asio::async_write(
                        context.socket->socket_handle(),
                        asio::buffer(context.source, context.source_size),
                        [&](const asio::error_code& error,
                            size_t bytes_transferred) {
                          if (!context.completed) {
                            context.completed = true;

                            if (!error) {
                              k.Start(bytes_transferred);
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

[[nodiscard]] inline auto Socket::Close() {
  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(this)
          .start([](Socket*& socket,
                    auto& k,
                    std::optional<Interrupt::Handler>& handler) {
            asio::post(
                socket->io_context(),
                [&]() {
                  if (handler.has_value()) {
                    if (handler->interrupt().Triggered()) {
                      k.Stop();
                      return;
                    }
                  }

                  if (!socket->IsOpen()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;
                  socket->socket_handle().close(error);

                  if (!error) {
                    socket->is_connected_ = false;
                    socket->is_open_.store(false);
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
