#pragma once

#include "eventuals/event-loop.h"
#include "eventuals/stream.h"
#include "glog/logging.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {

////////////////////////////////////////////////////////////////////////

class AcceptedSocket {
 public:
  // NOTE: Current std::future implementation in MSVC STL requires default
  // constructor.
  AcceptedSocket() {}

  AcceptedSocket(EventLoop& loop, uv_tcp_t* tcp_)
    : loop_(&loop),
      tcp_(tcp_) {
  }

  AcceptedSocket(const AcceptedSocket& that) = delete;
  AcceptedSocket(AcceptedSocket&& that) {
    std::swap(loop_, that.loop_);
    std::swap(tcp_, that.tcp_);
  };

  AcceptedSocket& operator=(const AcceptedSocket& that) = delete;
  AcceptedSocket& operator=(AcceptedSocket&& that) {
    if (this == &that) {
      return *this;
    }

    loop_ = that.loop_;
    that.loop_ = nullptr;
    tcp_ = that.tcp_;
    that.tcp_ = nullptr;

    return *this;
  }

  auto Read(const size_t& bytes_to_read) {
    CHECK_NOTNULL(loop_);
    CHECK_NOTNULL(tcp_);

    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the signal has
    // fired (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow '&loop' so accept can't outlive a loop.
        _Read::Composable{loop(), tcp_, bytes_to_read});
  }

  auto Write(const std::string& data) {
    CHECK_NOTNULL(loop_);
    CHECK_NOTNULL(tcp_);

    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the signal has
    // fired (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow '&loop' so accept can't outlive a loop.
        _Write::Composable{loop(), tcp_, data});
  }

  auto Shutdown() {
    CHECK_NOTNULL(loop_);
    CHECK_NOTNULL(tcp_);

    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the signal has
    // fired (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow '&loop' so accept can't outlive a loop.
        _Shutdown::Composable{loop(), tcp_});
  }

  auto Close() {
    CHECK_NOTNULL(loop_);
    CHECK_NOTNULL(tcp_);

    // NOTE: we use a 'RescheduleAfter()' to ensure we use current
    // scheduling context to invoke the continuation after the signal has
    // fired (or was interrupted).
    return RescheduleAfter(
        // TODO(benh): borrow '&loop' so accept can't outlive a loop.
        _Close::Composable{loop(), tcp_});
  }

 private:
  EventLoop& loop() {
    return *loop_;
  }

  EventLoop* loop_ = nullptr;
  uv_tcp_t* tcp_ = nullptr;

  struct _Read {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, EventLoop& loop, uv_tcp_t* tcp, size_t bytes_to_read)
        : k_(std::move(k)),
          loop_(loop),
          tcp_(tcp),
          bytes_to_read_(bytes_to_read),
          start_(&loop, "AcceptedSocket::Read (start)"),
          interrupt_(&loop, "AcceptedSocket::Read (interrupt)") {
      }

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          loop_(that.loop_),
          tcp_(that.tcp_),
          bytes_to_read_(that.bytes_to_read_),
          start_(&that.loop_, "AcceptedSocket::Read (start)"),
          interrupt_(&that.loop_, "AcceptedSocket::Read (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);
        CHECK_NOTNULL(tcp_);

        loop_.Submit(
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
          loop_.Submit(
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
      // Adaptors to libuv functions.
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(tcp_);
      }

      K_ k_;

      EventLoop& loop_;
      uv_tcp_t* tcp_ = nullptr;
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
        return Continuation<K>{std::move(k), loop_, tcp_, bytes_to_read_};
      }

      EventLoop& loop_;
      uv_tcp_t* tcp_;
      size_t bytes_to_read_;
    };
  };

  struct _Write {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, EventLoop& loop, uv_tcp_t* tcp, std::string&& data)
        : k_(std::move(k)),
          loop_(loop),
          tcp_(tcp),
          buffer_(std::move(data)),
          start_(&loop, "AcceptedSocket::Write (start)"),
          interrupt_(&loop, "AcceptedSocket::Write (interrupt)") {
      }

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          loop_(that.loop_),
          tcp_(that.tcp_),
          buffer_(std::move(that.buffer_)),
          start_(&that.loop_, "AcceptedSocket::Write (start)"),
          interrupt_(&that.loop_, "AcceptedSocket::Write (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);
        CHECK_NOTNULL(tcp_);

        loop_.Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                uv_req_set_data(
                    req(),
                    this);

                int status = uv_write(
                    write(),
                    stream(),
                    buffer_,
                    1,
                    [](uv_write_t* request, int status) {
                      auto& continuation = *(Continuation*) request->data;

                      if (!continuation.completed_) {
                        if (status == 0) {
                          continuation.completed_ = true;
                          continuation.k_.Start();
                        } else {
                          continuation.completed_ = true;
                          continuation.k_.Fail(uv_strerror(status));
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

      // NOTE: Already started uv_write can't be interrupted.
      // http://docs.libuv.org/en/v1.x/request.html?highlight=uv_req_t#c.uv_cancel #exceed80
      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop_.Submit(
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
      // Adaptors to libuv functions.
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(tcp_);
      }

      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(tcp_);
      }

      uv_req_t* req() {
        return reinterpret_cast<uv_req_t*>(&write_);
      }

      uv_write_t* write() {
        return &write_;
      }

      K_ k_;

      EventLoop& loop_;
      uv_tcp_t* tcp_ = nullptr;
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
        return Continuation<K>{std::move(k), loop_, tcp_, std::move(data_)};
      }

      EventLoop& loop_;
      uv_tcp_t* tcp_;
      std::string data_;
    };
  };

  struct _Shutdown {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, EventLoop& loop, uv_tcp_t* tcp)
        : k_(std::move(k)),
          loop_(loop),
          tcp_(tcp),
          start_(&loop, "AcceptedSocket::Shutdown (start)"),
          interrupt_(&loop, "AcceptedSocket::Shutdown (interrupt)") {
      }

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          loop_(that.loop_),
          tcp_(that.tcp_),
          start_(&that.loop_, "AcceptedSocket::Shutdown (start)"),
          interrupt_(&that.loop_, "AcceptedSocket::Shutdown (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);
        CHECK_NOTNULL(tcp_);

        loop_.Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                uv_req_set_data(
                    shutdown(),
                    this);

                int status = uv_shutdown(
                    shutdown(),
                    stream(),
                    [](uv_shutdown_t* request, int status) {
                      auto& continuation = *(Continuation*) request->data;

                      if (!continuation.completed_) {
                        if (status == 0) {
                          continuation.completed_ = true;
                          continuation.k_.Start();
                        } else {
                          continuation.completed_ = true;
                          continuation.k_.Fail(uv_strerror(status));
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

      // NOTE: Already started uv_shutdown can't be interrupted.
      // http://docs.libuv.org/en/v1.x/request.html?highlight=uv_req_t#c.uv_cancel #exceed80
      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop_.Submit(
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
      // Adaptors to libuv functions.
      uv_stream_t* stream() {
        return reinterpret_cast<uv_stream_t*>(tcp_);
      }

      uv_req_t* req() {
        return reinterpret_cast<uv_req_t*>(&shutdown_);
      }

      uv_shutdown_t* shutdown() {
        return &shutdown_;
      }

      K_ k_;

      EventLoop& loop_;
      uv_tcp_t* tcp_ = nullptr;

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
        return Continuation<K>{std::move(k), loop_, tcp_};
      }

      EventLoop& loop_;
      uv_tcp_t* tcp_;
    };
  };

  struct _Close {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, EventLoop& loop, uv_tcp_t* tcp)
        : k_(std::move(k)),
          loop_(loop),
          tcp_(tcp),
          start_(&loop, "AcceptedSocket::Close (start)"),
          interrupt_(&loop, "AcceptedSocket::Close (interrupt)") {
      }

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          loop_(that.loop_),
          tcp_(that.tcp_),
          start_(&that.loop_, "AcceptedSocket::Close (start)"),
          interrupt_(&that.loop_, "AcceptedSocket::Close (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_ && !completed_);
        CHECK_NOTNULL(tcp_);

        loop_.Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                uv_handle_set_data(
                    handle(),
                    this);

                uv_close(
                    handle(),
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;

                      delete handle;

                      if (!continuation.completed_) {
                        continuation.completed_ = true;
                        continuation.k_.Start();
                      } else {
                        // Was interrupted.
                        continuation.k_.Stop();
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

      // NOTE: Interrupting uv_close is not possible and will lead to
      // leaving an active handle inside EventLoop, hence the
      // handle should be closed anyway.
      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop_.Submit(
              [this]() {
                if (!started_) {
                  completed_ = true;

                  uv_handle_set_data(
                      handle(),
                      this);

                  uv_close(
                      handle(),
                      [](uv_handle_t* handle) {
                        auto& continuation = *(Continuation*) handle->data;

                        delete handle;

                        continuation.k_.Stop();
                      });
                } else if (!completed_) {
                  completed_ = true;
                  // NOTE: uv_close will call k_.Stop() in its callback.
                }
              },
              &interrupt_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      // Adaptors to libuv functions.
      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(tcp_);
      }

      K_ k_;

      EventLoop& loop_;
      uv_tcp_t* tcp_ = nullptr;

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
        return Continuation<K>{std::move(k), loop_, tcp_};
      }

      EventLoop& loop_;
      uv_tcp_t* tcp_;
    };
  };
};

////////////////////////////////////////////////////////////////////////

struct _AcceptOnce {
  template <typename K_>
  struct Continuation {
    Continuation(K_ k, EventLoop& loop, std::string&& ip, const int port)
      : k_{std::move(k)},
        loop_(loop),
        ip_(std::move(ip)),
        port_(port),
        start_(&loop, "Accept (start)"),
        interrupt_(&loop, "Accept (interrupt)") {}

    Continuation(Continuation&& that)
      : k_{std::move(that.k_)},
        loop_(that.loop_),
        ip_(std::move(that.ip_)),
        port_(that.port_),
        start_(&that.loop_, "Accept (start)"),
        interrupt_(&that.loop_, "Accept (interrupt)") {
      CHECK(!that.started_ || !that.completed_) << "moving after starting";
      CHECK(!handler_);
    }

    ~Continuation() {
      CHECK(!loop_.InEventLoop())
          << "attempting to destruct a signal on the event loop "
          << "is unsupported as it may lead to a deadlock";

      if (started_) {
        // Wait until we can destruct the signal/handle.
        while (!closed_.load()) {}
      }
    }

    void Start() {
      CHECK(!started_ && !completed_);

      loop_.Submit(
          [this]() {
            if (!completed_) {
              started_ = true;

              if (port_ < 0 || port_ > 65535) {
                completed_ = true;
                closed_.store(true);
                k_.Fail("Invalid port");
                return;
              }

              int status = 0;

              status = uv_tcp_init(loop_, tcp());
              if (status != 0) {
                completed_ = true;
                closed_.store(true);
                k_.Fail(uv_strerror(status));
                return;
              }

              uv_handle_set_data(
                  handle(),
                  this);

              sockaddr_in addr = {};
              status = uv_ip4_addr(ip_.c_str(), port_, &addr);
              if (status != 0) {
                completed_ = true;
                uv_close(
                    handle(),
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_.store(true);
                    });
                k_.Fail(uv_strerror(status));
                return;
              }

              status = uv_tcp_bind(
                  tcp(),
                  reinterpret_cast<const sockaddr*>(&addr),
                  0);
              if (status != 0) {
                completed_ = true;
                uv_close(
                    handle(),
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_.store(true);
                    });
                k_.Fail(uv_strerror(status));
                return;
              }

              status = uv_listen(
                  stream(),
                  SOMAXCONN,
                  [](uv_stream_t* server, int status) {
                    auto& continuation = *(Continuation*) server->data;

                    continuation.completed_ = true;

                    if (status == 0) {
                      uv_tcp_t* client = new uv_tcp_t();
                      uv_tcp_init(continuation.loop_, client);
                      uv_accept(
                          reinterpret_cast<uv_stream_t*>(server),
                          reinterpret_cast<uv_stream_t*>(client));

                      uv_close(
                          (uv_handle_t*) server,
                          [](uv_handle_t* handle) {
                            auto& continuation = *(Continuation*) handle->data;
                            continuation.closed_.store(true);
                          });

                      continuation.k_.Start(
                          AcceptedSocket(continuation.loop_, client));
                    } else {
                      uv_close(
                          (uv_handle_t*) server,
                          [](uv_handle_t* handle) {
                            auto& continuation = *(Continuation*) handle->data;
                            continuation.closed_.store(true);
                          });

                      continuation.k_.Fail(uv_strerror(status));
                    }
                  });
              if (status != 0) {
                completed_ = true;
                uv_close(
                    handle(),
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_.store(true);
                    });
                k_.Fail(uv_strerror(status));
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
        loop_.Submit(
            [this]() {
              if (!started_) {
                CHECK(!completed_);
                completed_ = true;
                k_.Stop();
              } else if (!completed_) {
                CHECK(started_);
                completed_ = true;

                uv_close(
                    handle(),
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_.store(true);
                    });

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
    // Adaptors to libuv functions.
    uv_tcp_t* tcp() {
      return &tcp_;
    }

    uv_stream_t* stream() {
      return reinterpret_cast<uv_stream_t*>(&tcp_);
    }

    uv_handle_t* handle() {
      return reinterpret_cast<uv_handle_t*>(&tcp_);
    }

    K_ k_;
    EventLoop& loop_;
    std::string ip_;
    int port_;

    uv_tcp_t tcp_;

    bool started_ = false;
    bool completed_ = false;

    std::atomic<bool> closed_ = false;

    EventLoop::Waiter start_;
    EventLoop::Waiter interrupt_;

    std::optional<Interrupt::Handler> handler_;
  };

  struct Composable {
    template <typename>
    using ValueFrom = AcceptedSocket;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K>{std::move(k), loop_, std::move(ip_), port_};
    }

    EventLoop& loop_;
    std::string ip_;
    int port_;
  };
};

////////////////////////////////////////////////////////////////////////

inline auto AcceptOnce(
    EventLoop& loop,
    const std::string& ip = "0.0.0.0",
    const int port = 0) {
  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the signal has
  // fired (or was interrupted).
  return RescheduleAfter(
      // TODO(benh): borrow '&loop' so accept can't outlive a loop.
      _AcceptOnce::Composable{loop, ip, port});
}

inline auto AcceptOnce(
    const std::string& ip = "0.0.0.0",
    const int port = 0) {
  return AcceptOnce(EventLoop::Default(), ip, port);
}

////////////////////////////////////////////////////////////////////////

} // namespace tcp
} // namespace ip
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
