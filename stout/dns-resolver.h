#pragma once

#include "stout/event-loop.h"
#include "stout/eventual.h"

namespace stout {
namespace eventuals {

inline auto DomainNameResolve(
    stout::eventuals::EventLoop& loop,
    const std::string& address,
    const std::string& port) {
  struct Data {
    EventLoop& loop;
    std::string addr;
    std::string port;
    void* k; // We should store our continuation
        // in this field , because we can't use
        // capturing lambdas as callbacks for libuv!
        // the link down will be helpfull:
        // https://misfra.me/2016/02/24/libuv-and-cpp/

    addrinfo hints;
    uv_getaddrinfo_t resolver;
    EventLoop::Callback start;
  };

  return stout::eventuals::Eventual<std::string>()
      .context(Data{
          loop,
          address,
          port,
          nullptr,
          {0, PF_INET, SOCK_STREAM, IPPROTO_TCP}})
      .start([](auto& data_context, auto& k) {
        // Storing continuation and Data structure
        using K = std::decay_t<decltype(k)>;
        data_context.k = static_cast<void*>(&k);
        data_context.resolver.data = &data_context;

        data_context.start = [&data_context](EventLoop& loop) {
          // Callback
          auto p_get_addr_info_cb = [](uv_getaddrinfo_t* request,
                                       int status,
                                       struct addrinfo* result) {
            Data* data = static_cast<Data*>(request->data);
            if (status < 0) {
              (static_cast<K*>(data->k))
                  ->Fail(std::string{uv_err_name(status)});
            } else {
              // Array "addr" is for resulting ip from the specific node
              char addr[17] = {'\0'};

              // Put to addr the resulting ip
              auto error = uv_ip4_name(
                  (struct sockaddr_in*) result->ai_addr,
                  addr,
                  16);

              // If there was an error we just fail our continuation
              if (error) {
                (static_cast<K*>(data->k))
                    ->Fail(std::string{uv_err_name(error)});
              } else {
                // Succeed the resulting ip
                uv_freeaddrinfo(result);
                (static_cast<K*>(data->k))
                    ->Start(std::string{addr});
              }
            }
          };
          auto error = uv_getaddrinfo(
              loop,
              &(data_context.resolver),
              p_get_addr_info_cb,
              data_context.addr.c_str(),
              data_context.port.c_str(),
              &(data_context.hints));

          // If there was an error we just fail our continuation
          if (error) {
            (static_cast<K*>(data_context.k))
                ->Fail(std::string{uv_err_name(error)});
          }
        };

        data_context.loop.Invoke(&data_context.start);
      });
  // TODO (Artur): think later about implementing
  // .interrupt([](auto &data, auto &k){...}); callback
}

inline auto DomainNameResolve(
    const std::string& address,
    const std::string& port) {
  return DomainNameResolve(EventLoop::Default(), address, port);
}

} // namespace eventuals
} // namespace stout
