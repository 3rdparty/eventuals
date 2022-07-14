#pragma once

#include "eventuals/event-loop.h"
#include "eventuals/tcp-base.h"
#include "eventuals/tcp-ssl-context.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {
namespace ssl {

////////////////////////////////////////////////////////////////////////

// Different handshake types.
enum class HandshakeType {
  // Perform handshaking as a client.
  CLIENT = asio::ssl::stream<asio::ip::tcp::socket>::handshake_type::client,

  // Perform handshaking as a server.
  SERVER = asio::ssl::stream<asio::ip::tcp::socket>::handshake_type::server
};

////////////////////////////////////////////////////////////////////////

class Socket final : public SocketBase {
 public:
  Socket(
      SSLContext& context,
      Protocol protocol,
      EventLoop& loop = EventLoop::Default())
    : SocketBase(protocol, loop),
      stream_(loop.io_context(), context.ssl_context_handle()) {
    // NOTE: Compiling this class produces
    // 'unused-private-field' warnings, hence why this
    // piece of code is needed.
    (void) completed_handshake_;
  }

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

  [[nodiscard]] auto Handshake(HandshakeType handshake_type);

  [[nodiscard]] auto Receive(
      void* destination,
      size_t destination_size,
      size_t bytes_to_read);

  [[nodiscard]] auto Send(const void* source, size_t source_size);

  [[nodiscard]] auto Close();

 private:
  asio::ip::tcp::socket& socket_handle() override {
    return stream_.next_layer();
  }

  asio::ssl::stream<asio::ip::tcp::socket>& stream_handle() {
    return stream_;
  }

  // This variable is only accessed or modified inside event loop,
  // so we don't need std::atomic wrapper.
  bool completed_handshake_ = false;

  asio::ssl::stream<asio::ip::tcp::socket> stream_;
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Socket::Handshake(HandshakeType handshake_type) {
  struct Data {
    Socket* socket;
    HandshakeType handshake_type;

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
          .context(Data{this, handshake_type})
          .start([](auto& data, auto& k, Interrupt::Handler& handler) {
            using K = std::decay_t<decltype(k)>;
            data.k = &k;

            handler.Install([&data]() {
              asio::post(data.socket->io_context(), [&]() {
                K& k = *static_cast<K*>(data.k);

                if (!data.started) {
                  data.completed = true;
                  k.Stop();
                } else if (!data.completed) {
                  data.completed = true;
                  asio::error_code error;
                  data.socket->socket_handle().cancel(error);

                  if (!error) {
                    k.Stop();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
            });

            asio::post(
                data.socket->io_context(),
                [&]() {
                  if (!data.completed) {
                    if (handler.interrupt().Triggered()) {
                      data.completed = true;
                      k.Stop();
                      return;
                    }

                    CHECK(!data.started);
                    data.started = true;

                    if (!data.socket->IsOpen()) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is closed"));
                      return;
                    }

                    if (!data.socket->is_connected_) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is not connected"));
                      return;
                    }

                    if (data.socket->completed_handshake_) {
                      data.completed = true;
                      k.Fail(
                          std::runtime_error(
                              "Handshake was already completed"));
                      return;
                    }

                    data.socket->stream_handle().async_handshake(
                        static_cast<
                            asio::ssl::stream<
                                asio::ip::tcp::socket>::handshake_type>(
                            data.handshake_type),
                        [&](const asio::error_code& error) {
                          if (!data.completed) {
                            data.completed = true;

                            if (!error) {
                              data.socket->completed_handshake_ = true;
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

[[nodiscard]] inline auto Socket::Receive(
    void* destination,
    size_t destination_size,
    size_t bytes_to_read) {
  struct Data {
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
          .context(Data{this, destination, destination_size, bytes_to_read})
          .start([](auto& data, auto& k, Interrupt::Handler& handler) {
            using K = std::decay_t<decltype(k)>;
            data.k = &k;

            handler.Install([&data]() {
              asio::post(data.socket->io_context(), [&]() {
                K& k = *static_cast<K*>(data.k);

                if (!data.started) {
                  data.completed = true;
                  k.Stop();
                } else if (!data.completed) {
                  data.completed = true;
                  asio::error_code error;
                  data.socket->socket_handle().cancel(error);

                  if (!error) {
                    k.Stop();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
            });

            asio::post(
                data.socket->io_context(),
                [&]() {
                  if (!data.completed) {
                    if (handler.interrupt().Triggered()) {
                      data.completed = true;
                      k.Stop();
                      return;
                    }

                    CHECK(!data.started);
                    data.started = true;

                    if (!data.socket->IsOpen()) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is closed"));
                      return;
                    }

                    if (!data.socket->is_connected_) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is not connected"));
                      return;
                    }

                    if (!data.socket->completed_handshake_) {
                      data.completed = true;
                      k.Fail(
                          std::runtime_error(
                              "Must Handshake before trying to Receive"));
                      return;
                    }

                    // Do not allow to read more than destination_size.
                    data.bytes_to_read = std::min(
                        data.bytes_to_read,
                        data.destination_size);

                    // Do not call async_read() if there're 0 bytes to be read.
                    if (data.bytes_to_read == 0) {
                      data.completed = true;
                      k.Start(0);
                      return;
                    }

                    // Start receiving.
                    // Will only succeed after the supplied buffer is full.
                    asio::async_read(
                        data.socket->stream_handle(),
                        asio::buffer(data.destination, data.bytes_to_read),
                        [&](const asio::error_code& error,
                            size_t bytes_transferred) {
                          if (!data.completed) {
                            data.completed = true;

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
  struct Data {
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
          .context(Data{this, source, source_size})
          .start([](auto& data, auto& k, Interrupt::Handler& handler) {
            using K = std::decay_t<decltype(k)>;
            data.k = &k;

            handler.Install([&data]() {
              asio::post(data.socket->io_context(), [&]() {
                K& k = *static_cast<K*>(data.k);

                if (!data.started) {
                  data.completed = true;
                  k.Stop();
                } else if (!data.completed) {
                  data.completed = true;
                  asio::error_code error;
                  data.socket->socket_handle().cancel(error);

                  if (!error) {
                    k.Stop();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
            });

            asio::post(
                data.socket->io_context(),
                [&]() {
                  if (!data.completed) {
                    if (handler.interrupt().Triggered()) {
                      data.completed = true;
                      k.Stop();
                      return;
                    }

                    CHECK(!data.started);
                    data.started = true;

                    if (!data.socket->IsOpen()) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is closed"));
                      return;
                    }

                    if (!data.socket->is_connected_) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is not connected"));
                      return;
                    }

                    if (!data.socket->completed_handshake_) {
                      data.completed = true;
                      k.Fail(
                          std::runtime_error(
                              "Must Handshake before trying to Send"));
                      return;
                    }

                    // Do not call async_write()
                    // if there're 0 bytes to be sent.
                    if (data.source_size == 0) {
                      data.completed = true;
                      k.Start(0);
                      return;
                    }

                    // Will only succeed after
                    // writing all of the data to socket.
                    asio::async_write(
                        data.socket->stream_handle(),
                        asio::buffer(data.source, data.source_size),
                        [&](const asio::error_code& error,
                            size_t bytes_transferred) {
                          if (!data.completed) {
                            data.completed = true;

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
          .start([](auto& socket, auto& k, Interrupt::Handler& handler) {
            asio::post(
                socket->io_context(),
                [&]() {
                  if (handler.interrupt().Triggered()) {
                    k.Stop();
                    return;
                  }

                  if (!socket->IsOpen()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;
                  socket->socket_handle().close(error);

                  if (!error) {
                    socket->is_connected_ = false;
                    socket->completed_handshake_ = false;
                    socket->is_open_.store(false);
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

} // namespace ssl
} // namespace tcp
} // namespace ip
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
