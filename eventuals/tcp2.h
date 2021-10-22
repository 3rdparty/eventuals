#pragma once

#include "eventuals/event-loop.h"
#include "uv.h"

namespace eventuals {
namespace ip {
namespace tcp {

class Socket {
 public:
  Socket()
    : loop_(EventLoop::Default()) {}

  Socket(EventLoop& loop)
    : loop_(loop) {}

  Socket(const Socket& that) = delete;
  Socket(Socket&& that) = delete;

  Socket& operator=(const Socket& that) = delete;
  Socket& operator=(Socket&& that) = delete;

  ~Socket() {
    CHECK(closed_);
  }

  auto Initialize() {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the socket was
    // initialized (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _Initialize::Composable{*this});
  }

  auto Bind(const std::string& ip, uint16_t port) {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the socket was
    // binded (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _Bind::Composable{*this, ip, port});
  }

  auto Connect(const std::string& ip, uint16_t port) {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the socket was
    // binded (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _Connect::Composable{*this, ip, port});
  }

  auto Listen() {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the socket was
    // put into listening state (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _Listen::Composable{*this});
  }

  //auto Accept() {
  //  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  //  // scheduling context to invoke the continuation after accepting
  //  // connection (or was interrupted).
  //  return RescheduleAfter(
  //      // TODO(benh): borrow 'this' so initialize can't outlive a loop.
  //      _Accept::Composable{*this});
  //}

  auto Accept(Socket* socket) {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after accepting
    // connection (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _Accept::Composable{*this, socket});
  }

  // auto Read(size_t bytes_to_read) {}

  // auto Read(void* buffer, size_t size_of_buffer) -> returns size_of_read_data
  // auto ReadAll() <- until EOF

  auto Receive(void* buffer, size_t buffer_size) {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after finishing
    // reading (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _ReceiveToBuffer::Composable{*this, buffer, buffer_size});
  }

  auto ReceiveExactly(size_t bytes_to_read) {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after finishing
    // reading (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _ReceiveExactly::Composable{*this, bytes_to_read});
  }

  auto Send(const std::string& data) {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after finishing
    // all writing (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so initialize can't outlive a loop.
        _Send::Composable{*this, data});
  }

  auto Shutdown() {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the socket was closed
    // (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so Close can't outlive a loop.
        _Shutdown::Composable{*this});
  }

  auto Close() {
    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the socket was closed
    // (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow 'this' so Close can't outlive a loop.
        _Close::Composable{*this});
  }

 private:
  EventLoop& loop_;
  uv_tcp_t tcp_;

  size_t connections_waiting_for_accept_ = 0;

  //bool heap_allocated_ = false;

  bool closed_ = true;

  // EventLoop::Buffer temporary_

  // Allocates memory on heap for a new Socket
  // and initializes it using loop from 'this'.
  // Failing to allocate memory returns a nullptr.
  // Failing to initialize the Socket with loop
  // returns a libuv error code.
  //std::variant<Socket*, int> CreateAndInitializeSocket() {
  //  CHECK(loop_.InEventLoop());
  //
  //  std::variant<Socket*, int> result;
  //
  //  Socket* socket_ptr = new (std::nothrow) Socket;
  //  if (socket_ptr == nullptr) {
  //    result.emplace<0>(nullptr);
  //    return result;
  //  }
  //
  //  int error = uv_tcp_init(loop_, &socket_ptr->tcp_);
  //  if (error) {
  //    delete socket_ptr;
  //    result.emplace<1>(error);
  //    return result;
  //  } else {
  //    // Stating a Socket to be heap allocated here because otherwise destructor
  //    // will try to delete the same pointer twice.
  //    socket_ptr->heap_allocated_ = true;
  //    result.emplace<0>(socket_ptr);
  //    return result;
  //  }
  //}

  struct _Initialize {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket)
        : k_(std::move(k)),
          socket_(socket),
          start_(&socket.loop_, "Socket::Initialize (start)"),
          interrupt_(&socket.loop_, "Socket::Initialize (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          start_(&that.socket_.loop_, "Socket::Initialize (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Initialize (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(socket_.closed_);

                started_ = true;

                int error = uv_tcp_init(loop(), tcp());

                if (error == 0) {
                  completed_ = true;
                  socket_.closed_ = false;
                  k_.Start();
                } else {
                  completed_ = true;
                  k_.Fail(uv_strerror(error));
                }
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_};
      }

      Socket& socket_;
    };
  };

  struct _Bind {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket, std::string&& ip, const uint16_t& port)
        : k_(std::move(k)),
          socket_(socket),
          ip_(std::move(ip)),
          port_(port),
          start_(&socket.loop_, "Socket::Bind (start)"),
          interrupt_(&socket.loop_, "Socket::Bind (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          ip_(std::move(that.ip_)),
          port_(that.port_),
          start_(&that.socket_.loop_, "Socket::Bind (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Bind (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(!socket_.closed_);

                started_ = true;

                sockaddr_in addr = {};
                int error = uv_ip4_addr(ip_.c_str(), port_, &addr);
                if (error != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(error));
                  return;
                }

                error = uv_tcp_bind(
                    tcp(),
                    reinterpret_cast<const sockaddr*>(&addr),
                    0);
                if (error != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(error));
                  return;
                }

                completed_ = true;
                k_.Start();
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;
      std::string ip_;
      uint16_t port_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_, std::move(ip_), port_};
      }

      Socket& socket_;
      std::string ip_;
      uint16_t port_;
    };
  };

  struct _Connect {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket, std::string&& ip, const uint16_t& port)
        : k_(std::move(k)),
          socket_(socket),
          ip_(std::move(ip)),
          port_(port),
          start_(&socket.loop_, "Socket::Connect (start)"),
          interrupt_(&socket.loop_, "Socket::Connect (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          ip_(std::move(that.ip_)),
          port_(that.port_),
          start_(&that.socket_.loop_, "Socket::Connect (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Connect (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(!socket_.closed_);

                started_ = true;

                sockaddr_in addr = {};
                int error = uv_ip4_addr(ip_.c_str(), port_, &addr);
                if (error != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(error));
                  return;
                }

                uv_req_set_data(req(), this);

                error = uv_tcp_connect(
                    connect(),
                    tcp(),
                    reinterpret_cast<sockaddr*>(&addr),
                    [](uv_connect_t* connect, int error) {
                      Continuation& continuation = *(Continuation*) uv_req_get_data((uv_req_t*) connect);

                      if (!continuation.completed_) {
                        continuation.completed_ = true;

                        if (error == 0) {
                          continuation.k_.Start();
                        } else {
                          continuation.k_.Fail(uv_strerror(error));
                        }
                      }
                    });

                if (error != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(error));
                  return;
                }
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      uv_connect_t* connect() {
        return &connect_;
      }

      uv_req_t* req() {
        return reinterpret_cast<uv_req_t*>(&connect_);
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;
      std::string ip_;
      uint16_t port_;

      uv_connect_t connect_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_, std::move(ip_), port_};
      }

      Socket& socket_;
      std::string ip_;
      uint16_t port_;
    };
  };

  struct _Listen {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket)
        : k_(std::move(k)),
          socket_(socket),
          start_(&socket.loop_, "Socket::Listen (start)"),
          interrupt_(&socket.loop_, "Socket::Listen (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          start_(&that.socket_.loop_, "Socket::Listen (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Listen (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                uv_handle_set_data(handle(), this);

                int error = uv_listen(
                    stream(),
                    SOMAXCONN,
                    [](uv_stream_t* server, int error) {
                      auto& continuation = *(Continuation*) server->data;

                      if (!error) {
                        ++(continuation.socket_.connections_waiting_for_accept_);
                      }
                    });

                completed_ = true;
                if (error == 0) {
                  k_.Start();
                } else {
                  k_.Fail(uv_strerror(error));
                }
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_};
      }

      Socket& socket_;
    };
  };

  struct _Accept {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket, Socket* to_socket)
        : k_(std::move(k)),
          socket_(socket),
          to_socket_(to_socket),
          start_(&socket.loop_, "Socket::Accept (start)"),
          interrupt_(&socket.loop_, "Socket::Accept (interrupt)") {
        CHECK_NOTNULL(to_socket_);
      }

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          to_socket_(that.to_socket_),
          start_(&that.socket_.loop_, "Socket::Accept (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Accept (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
        CHECK_NOTNULL(that.to_socket_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                // Using prepare handle so that we can check for
                // incoming connection without blocking the running thread.

                // This functions always succeeds.
                // Returns 0.
                uv_idle_init(loop(), &idle_);
                uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&idle_), this);
                uv_idle_start(
                    &idle_,
                    [](uv_idle_t* idle) {
                      auto& continuation = *(Continuation*) idle->data;

                      // We only accept the connection
                      // if we have an incoming connection.
                      // Otherwise uv_accept has UB.
                      if (continuation.socket_.connections_waiting_for_accept_ > 0) {
                        continuation.completed_ = true;

                        // Socket allocation/initialization.
                        // TODO: proper fail behaviour.
                        //if (continuation.to_socket_ == nullptr) {
                        //  std::variant<Socket*, int> variant = continuation.socket_.CreateAndInitializeSocket();
                        //  Socket** Socket_v = std::get_if<Socket*>(&variant);
                        //  int* int_v = std::get_if<int>(&variant);
                        //  if (Socket_v != nullptr) {
                        //    continuation.k_.Start(*Socket_v);
                        //    return;
                        //  } else {
                        //    continuation.k_.Fail(uv_strerror(*int_v));
                        //    return;
                        //  }
                        //} else {
                        //  int error = uv_tcp_init(continuation.loop(), &continuation.to_socket_->tcp_);
                        //  if (error == 0) {
                        //    continuation.socket_.initialized_.store(true);
                        //    continuation.socket_.closed_.store(false);
                        //  } else {
                        //    continuation.completed_ = true;
                        //    continuation.k_.Fail(uv_strerror(error));
                        //  }
                        //}

                        continuation.error_ = uv_tcp_init(continuation.loop(), &continuation.to_socket_->tcp_);
                        if (continuation.error_ == 0) {
                          continuation.to_socket_->closed_ = false;
                        } else {
                          continuation.completed_ = true;
                        }

                        if (continuation.error_ == 0) {
                          // Accepting connection.
                          continuation.error_ = uv_accept(continuation.stream(), (uv_stream_t*) &continuation.to_socket_->tcp_);
                        }

                        // Closing idle handle.
                        uv_idle_stop(idle);
                        uv_close((uv_handle_t*) idle, [](uv_handle_t* handle) {
                          auto& continuation = *(Continuation*) handle->data;

                          if (continuation.error_ == 0) {
                            --(continuation.socket_.connections_waiting_for_accept_);
                            continuation.k_.Start();
                          } else {
                            continuation.k_.Fail(uv_strerror(continuation.error_));
                          }
                        });
                      }
                    });
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;
      Socket* to_socket_;

      uv_idle_t idle_;

      bool started_ = false;
      bool completed_ = false;
      int error_ = 0;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_, to_socket_};
      }

      Socket& socket_;
      Socket* to_socket_;
    };
  };

  struct _ReceiveToBuffer {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket, void* buffer, const size_t& buffer_size)
        : k_(std::move(k)),
          socket_(socket),
          buffer_(buffer),
          buffer_size_(buffer_size),
          start_(&socket.loop_, "Socket::ReceiveToBuffer (start)"),
          interrupt_(&socket.loop_, "Socket::ReceiveToBuffer (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          buffer_(that.buffer_),
          buffer_size_(that.buffer_size_),
          start_(&that.socket_.loop_, "Socket::ReceiveToBuffer (start)"),
          interrupt_(&that.socket_.loop_, "Socket::ReceiveToBuffer (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(!socket_.closed_);

                started_ = true;

                uv_handle_set_data(
                    handle(),
                    this);

                int status = uv_read_start(
                    stream(),
                    [](uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buffer) {
                      buffer->base = new char[suggested_size];
                      buffer->len = suggested_size;
                    },
                    [](uv_stream_t* stream,
                       ssize_t nread,
                       const uv_buf_t* buffer) {
                      auto& continuation = *(Continuation*) stream->data;

                      CHECK(!continuation.completed_);
                      continuation.completed_ = true;

                      if (nread < 0) {
                        delete[] buffer->base;
                        uv_read_stop(stream);
                        continuation.k_.Fail(uv_strerror(nread));
                      } else {
                        // Casting 'nread' to 'size_t' since std::min requires
                        // arguments to be of the same type.
                        // 'nread' can't be less than 0 in here, so it is safe to do so.
                        auto& min = std::min(static_cast<size_t>(nread), continuation.buffer_size_);
                        memcpy(continuation.buffer_, buffer->base, min);
                        delete[] buffer->base;
                        uv_read_stop(stream);
                        continuation.k_.Start(min);
                      }
                    });
                if (status != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(status));
                }
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!started_) {
                  completed_ = true;
                  k_.Stop();
                } else if (!completed_) {
                  completed_ = true;
                  uv_read_stop(stream());
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      void* buffer_ = nullptr;
      size_t buffer_size_ = 0;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = size_t;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_, buffer_, buffer_size_};
      }

      Socket& socket_;
      void* buffer_ = nullptr;
      size_t buffer_size_ = 0;
    };
  };

  struct _ReceiveExactly {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket, const size_t& bytes_to_read)
        : k_(std::move(k)),
          socket_(socket),
          bytes_to_read_(bytes_to_read),
          start_(&socket.loop_, "Socket::ReceiveExactly (start)"),
          interrupt_(&socket.loop_, "Socket::ReceiveExactly (interrupt)") {
      }

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          bytes_to_read_(that.bytes_to_read_),
          start_(&that.socket_.loop_, "Socket::ReceiveExactly (start)"),
          interrupt_(&that.socket_.loop_, "Socket::ReceiveExactly (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                uv_handle_set_data(
                    handle(),
                    this);

                buffer_.Reserve(bytes_to_read_);

                int status = uv_read_start(
                    stream(),
                    [](uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buffer) {
                      buffer->base = new char[suggested_size];
                      buffer->len = suggested_size;
                    },
                    [](uv_stream_t* stream,
                       ssize_t nread,
                       const uv_buf_t* buffer) {
                      auto& continuation = *(Continuation*) stream->data;

                      if (nread < 0) {
                        continuation.completed_ = true;
                        delete[] buffer->base;
                        uv_read_stop(stream);
                        continuation.k_.Fail(uv_strerror(nread));
                      } else {
                        // NOTE: Converting to size_t to account
                        // for -Wsign-compare in GCC.
                        if ((size_t) nread < continuation.bytes_to_read_) {
                          continuation.buffer_ += std::string(buffer->base);
                          continuation.bytes_to_read_ -= nread;
                          delete[] buffer->base;
                        } else {
                          continuation.buffer_ += std::string(
                              buffer->base,
                              continuation.bytes_to_read_);
                          continuation.bytes_to_read_ = 0;

                          continuation.completed_ = true;
                          delete[] buffer->base;
                          uv_read_stop(stream);
                          continuation.k_.Start(continuation.buffer_.Extract());
                        }
                      }
                    });
                if (status != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(status));
                }
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!started_) {
                  completed_ = true;
                  k_.Stop();
                } else if (!completed_) {
                  completed_ = true;
                  uv_read_stop(stream());
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      size_t bytes_to_read_;

      EventLoop::Buffer buffer_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = std::string;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_, bytes_to_read_};
      }

      Socket& socket_;
      size_t bytes_to_read_;
    };
  };

  struct _Send {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket, std::string&& data)
        : k_(std::move(k)),
          socket_(socket),
          buffer_(std::move(data)),
          start_(&socket.loop_, "Socket::Send (start)"),
          interrupt_(&socket.loop_, "Socket::Send (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          buffer_(std::move(that.buffer_)),
          start_(&that.socket_.loop_, "Socket::Send (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Send (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(!socket_.closed_);

                started_ = true;

                uv_req_set_data(
                    req(),
                    this);

                int error = uv_write(
                    write(),
                    stream(),
                    buffer_,
                    1,
                    [](uv_write_t* request, int error) {
                      auto& continuation = *(Continuation*) request->data;

                      if (!continuation.completed_) {
                        continuation.completed_ = true;
                        if (error == 0) {
                          continuation.k_.Start();
                        } else {
                          continuation.k_.Fail(uv_strerror(error));
                        }
                      }
                    });
                if (error != 0) {
                  completed_ = true;
                  k_.Fail(uv_strerror(error));
                }
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      uv_req_t* req() {
        return reinterpret_cast<uv_req_t*>(&write_);
      }

      uv_write_t* write() {
        return &write_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      EventLoop::Buffer buffer_;
      uv_write_t write_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_, std::move(data_)};
      }

      Socket& socket_;
      std::string data_;
    };
  };

  struct _Shutdown {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket)
        : k_(std::move(k)),
          socket_(socket),
          start_(&socket.loop_, "Socket::Shutdown (start)"),
          interrupt_(&socket.loop_, "Socket::Shutdown (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          start_(&that.socket_.loop_, "Socket::Shutdown (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Shutdown (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(!socket_.closed_);

                started_ = true;

                uv_req_set_data(req(), this);

                uv_shutdown(
                    shutdown(),
                    stream(),
                    [](uv_shutdown_t* shutdown, int error) {
                      Continuation& continuation = *(Continuation*) uv_req_get_data((uv_req_t*) shutdown);

                      if (!continuation.completed_) {
                        continuation.completed_ = true;
                        if (error == 0) {
                          continuation.k_.Start();
                        } else {
                          continuation.k_.Fail(uv_strerror(error));
                        }
                      }
                    });
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      uv_shutdown_t* shutdown() {
        return &shutdown_;
      }

      uv_req_t* req() {
        return reinterpret_cast<uv_req_t*>(&shutdown_);
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      uv_shutdown_t shutdown_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_};
      }

      Socket& socket_;
    };
  };

  struct _Close {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Socket& socket)
        : k_(std::move(k)),
          socket_(socket),
          start_(&socket.loop_, "Socket::Close (start)"),
          interrupt_(&socket.loop_, "Socket::Close (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          socket_(that.socket_),
          start_(&that.socket_.loop_, "Socket::Close (start)"),
          interrupt_(&that.socket_.loop_, "Socket::Close (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop().Submit(
            [this]() {
              if (!completed_) {
                CHECK(!socket_.closed_);

                started_ = true;

                uv_handle_set_data(handle(), this);

                uv_close(handle(), [](uv_handle_t* handle) {
                  Continuation& continuation =
                      *(Continuation*) uv_handle_get_data(handle);

                  continuation.socket_.closed_ = true;

                  if (!continuation.completed_) {
                    continuation.completed_ = true;
                    continuation.k_.Start();
                  }
                });
              }
            },
            &start_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        k_.Fail(std::forward<Args>(args)...);
      }

      void Stop() {
        k_.Stop();
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop().Submit(
              [this]() {
                if (!completed_) {
                  completed_ = true;
                  k_.Stop();
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&socket_.tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(&socket_.tcp_);
      }

      uv_tcp_t* tcp() {
        return &socket_.tcp_;
      }

      EventLoop& loop() {
        return socket_.loop_;
      }

      K_ k_;

      Socket& socket_;

      bool started_ = false;
      bool completed_ = false;

      EventLoop::Waiter start_;
      EventLoop::Waiter interrupt_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), socket_};
      }

      Socket& socket_;
    };
  };
};

} // namespace tcp
} // namespace ip
} // namespace eventuals


// uv_read_start()
// []() {}

// uv_buffer_t

// uv_read_stop()