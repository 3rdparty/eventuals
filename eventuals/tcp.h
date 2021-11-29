#pragma once

#include <string>
#include <utility>

#include "eventuals/event-loop.h"

namespace eventuals {
namespace ip {
namespace tcp {

struct EndPoint {
  std::string ip;
  int port;
};

enum class Protocol {
  UNKNOWN,
  IPV4,
  IPV6,
};

class Socket;

class Acceptor {
 public:
  Acceptor()
    : loop_(&EventLoop::Default()),
      acceptor_(loop_->context()) {}

  Acceptor(EventLoop& loop)
    : loop_(&loop),
      acceptor_(loop_->context()) {}

  Acceptor(const Acceptor& that) = delete;
  Acceptor(Acceptor&& that)
    : loop_(that.loop_),
      acceptor_(std::move(that.acceptor_)) {
    that.was_moved_.store(true);
  }

  Acceptor& operator=(const Acceptor& that) = delete;
  Acceptor& operator=(Acceptor&& that) {
    loop_ = that.loop_;
    acceptor_ = std::move(that.acceptor_);

    that.was_moved_.store(true);

    return *this;
  };

  ~Acceptor() {}

  std::string address() {
    CHECK(!was_moved_.load());
    return acceptor().local_endpoint().address().to_string();
  }

  int port() {
    CHECK(!was_moved_.load());
    return acceptor().local_endpoint().port();
  }

  Protocol protocol() {
    return protocol_;
  }

  auto Open(Protocol protocol) {
    return RescheduleAfter(_Open::Composable{*this, protocol});
  }

  auto Bind(const std::string& ip, uint16_t port) {
    return RescheduleAfter(_Bind::Composable{*this, ip, port});
  }

  auto Listen() {
    return RescheduleAfter(_Listen::Composable{*this});
  }

  auto Accept() {
    return RescheduleAfter(_Accept::Composable{*this});
  }

 private:
  asio::ip::tcp::acceptor& acceptor() {
    return acceptor_;
  }

  EventLoop* loop_ = nullptr;
  asio::ip::tcp::acceptor acceptor_;
  Protocol protocol_ = Protocol::UNKNOWN;

  std::atomic<bool> was_moved_ = false;

  struct _Open {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Acceptor& acceptor, Protocol protocol)
        : k_(std::move(k)),
          acceptor_(acceptor),
          protocol_(protocol) {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          acceptor_(that.acceptor_),
          protocol_(that.protocol_) {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_.load() && !completed_.load());
        CHECK(!acceptor_.was_moved_.load());

        started_.store(true);
        completed_.store(true);

        acceptor_.protocol_ = protocol_;

        switch (protocol_) {
          case Protocol::IPV4:
            acceptor_.acceptor().open(asio::ip::tcp::v4());
            k_.Start();
            break;
          case Protocol::IPV6:
            acceptor_.acceptor().open(asio::ip::tcp::v6());
            k_.Start();
            break;
          default:
            k_.Fail("Invalid protocol");
        }
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

        handler_.emplace(
            &interrupt,
            [this]() {
              if (!completed_.load()) {
                completed_.store(true);
                k_.Stop();
              }
            });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      K_ k_;

      Acceptor& acceptor_;
      Protocol protocol_;

      std::atomic<bool> started_ = false;
      std::atomic<bool> completed_ = false;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), acceptor_, protocol_};
      }

      Acceptor& acceptor_;
      Protocol protocol_;
    };
  };

  struct _Bind {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Acceptor& acceptor, std::string&& ip, uint16_t port)
        : k_(std::move(k)),
          acceptor_(acceptor),
          ip_(std::move(ip)),
          port_(port) {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          acceptor_(that.acceptor_),
          ip_(std::move(that.ip_)),
          port_(that.port_) {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_.load() && !completed_.load());
        CHECK(!acceptor_.was_moved_.load());

        started_.store(true);
        completed_.store(true);

        asio::error_code ec;
        asio::ip::address address;

        switch (acceptor_.protocol()) {
          case Protocol::IPV4:
            address = asio::ip::address_v4::from_string(ip_, ec);
            break;
          case Protocol::IPV6:
            address = asio::ip::address_v6::from_string(ip_, ec);
            break;
          default:
            k_.Fail(std::string("Invalid protocol"));
            return;
        }

        auto endpoint = asio::ip::tcp::endpoint(address, port_);

        if (!ec) {
          acceptor_.acceptor().bind(endpoint);
          k_.Start();
        } else {
          k_.Fail(ec.message());
        }
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

        handler_.emplace(
            &interrupt,
            [this]() {
              if (!completed_.load()) {
                completed_.store(true);
                k_.Stop();
              }
            });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      K_ k_;

      Acceptor& acceptor_;
      std::string ip_;
      uint16_t port_;

      std::atomic<bool> started_ = false;
      std::atomic<bool> completed_ = false;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), acceptor_, std::move(ip_), port_};
      }

      Acceptor& acceptor_;
      std::string ip_;
      uint16_t port_;
    };
  };

  struct _Listen {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Acceptor& acceptor)
        : k_(std::move(k)),
          acceptor_(acceptor) {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          acceptor_(that.acceptor_) {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_.load() && !completed_.load());
        CHECK(!acceptor_.was_moved_.load());

        started_.store(true);
        completed_.store(true);

        asio::error_code ec;

        acceptor_.acceptor().listen(
            socket_base::max_listen_connections,
            ec);

        if (!ec) {
          k_.Start();
        } else {
          k_.Fail(ec.message());
        }
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

        handler_.emplace(
            &interrupt,
            [this]() {
              if (!completed_.load()) {
                completed_.store(true);
                k_.Stop();
              }
            });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      K_ k_;

      Acceptor& acceptor_;

      std::atomic<bool> started_ = false;
      std::atomic<bool> completed_ = false;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), acceptor_};
      }

      Acceptor& acceptor_;
    };
  };

  struct _Accept {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, Acceptor& acceptor)
        : k_(std::move(k)),
          acceptor_(acceptor) {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          acceptor_(that.acceptor_) {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      void Start() {
        CHECK(!started_.load() && !completed_.load());
        CHECK(!acceptor_.was_moved_.load());

        started_.store(true);
        completed_.store(true);

        acceptor_.acceptor().async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket s) {
          if (!ec) {
            Socket sock(s);
            k_.Start(std::move(sock));
          } else {
            k_.Fail(ec.message());
          }
        });
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

        handler_.emplace(
            &interrupt,
            [this]() {
              if (!started_.load()) {
                completed_.store(true);
                k_.Stop();
              } else if (!completed_.load()) {
                completed_.store(true);
                asio::error_code ec;
                acceptor_.acceptor().cancel(ec);
                if (!ec) {
                  k_.Stop();
                } else {
                  k_.Fail(ec.message());
                }
              }
            });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      K_ k_;

      Acceptor& acceptor_;

      std::atomic<bool> started_ = false;
      std::atomic<bool> completed_ = false;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), acceptor_};
      }

      Acceptor& acceptor_;
    };
  };
};

/*
class Socket {
 public:
  Socket()
    : loop_(&EventLoop::Default()),
      socket_(loop_->context()) {}

  Socket(EventLoop& loop)
    : loop_(&loop),
      socket_(loop_->context()) {}

  Socket(const Socket& that) = delete;
  Socket(Socket&& that)
    : loop_(that.loop_),
      socket_(std::move(that.socket_)) {
    that.was_moved_ = true;
  }

  Socket& operator=(const Socket& that) = delete;
  Socket& operator=(Socket&& that) {
    loop_ = that.loop_;
    socket_ = std::move(that.socket_);

    that.was_moved_ = true;
  };

  ~Socket() {}

 private:
  EventLoop* loop_ = nullptr;
  asio::ip::tcp::socket socket_;

  bool was_moved_ = false;
};
*/

} // namespace tcp
} // namespace ip
} // namespace eventuals