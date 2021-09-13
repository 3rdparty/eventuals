#pragma once

#include "stout/event-loop.h"
#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

inline auto DomainNameResolve(
    stout::eventuals::EventLoop& loop,
    const std::string& address,
    const std::string& port) {
  struct Data {
    EventLoop& loop;
    std::string address;
    std::string port;
    addrinfo hints = {0, PF_INET, SOCK_STREAM, IPPROTO_TCP};
    void* k = nullptr;
    uv_getaddrinfo_t resolver;
  };

  return loop.Schedule(
      "DomainNameResolve",
      Eventual<std::string>()
          .context(Data{loop, address, port})
          .start([](auto& data, auto& k) {
            using K = std::decay_t<decltype(k)>;

            data.k = static_cast<void*>(&k);
            data.resolver.data = &data;

            auto error = uv_getaddrinfo(
                data.loop,
                &(data.resolver),
                [](uv_getaddrinfo_t* request,
                   int status,
                   struct addrinfo* result) {
                  auto& data = *static_cast<Data*>(request->data);
                  if (status < 0) {
                    static_cast<K*>(data.k)
                        ->Fail(std::string{uv_err_name(status)});
                  } else {
                    // Array "ip" is resulting IPv4 for the specified address.
                    char ip[17] = {'\0'};

                    auto error = uv_ip4_name(
                        (struct sockaddr_in*) result->ai_addr,
                        ip,
                        16);

                    if (error) {
                      static_cast<K*>(data.k)
                          ->Fail(std::string{uv_err_name(error)});
                    } else {
                      uv_freeaddrinfo(result);
                      static_cast<K*>(data.k)->Start(std::string{ip});
                    }
                  }
                },
                data.address.c_str(),
                data.port.c_str(),
                &(data.hints));

            if (error) {
              static_cast<K*>(data.k)->Fail(std::string{uv_err_name(error)});
            }
          }));
  // TODO (Artur): think later about implementing
  // .interrupt([](auto &data, auto &k){...}); callback
}

////////////////////////////////////////////////////////////////////////

inline auto DomainNameResolve(
    const std::string& address,
    const std::string& port) {
  return DomainNameResolve(EventLoop::Default(), address, port);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
