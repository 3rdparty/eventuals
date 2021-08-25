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
    void* k_p; // we should store our continuation
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
      .start([](auto& data, auto& k) {
        // storing continuation and Data structure
        data.k_p = static_cast<void*>(&k);
        data.resolver.data = &data;

        data.start = [&data](EventLoop& loop) {
          // callback
          auto p_get_addr_info_cb = [](uv_getaddrinfo_t* req,
                                       int status,
                                       struct addrinfo* res) {
            Data* data_p = static_cast<Data*>(req->data);
            if (status < 0) {
              (*static_cast<decltype(&k)>(data_p->k_p))
                  .Fail(std::string{uv_err_name(status)});
            } else {
              // addr array is for resulting ip from the specific node
              char addr[17] = {'\0'};

              // get to addr the resulting ip
              auto result = uv_ip4_name(
                  (struct sockaddr_in*) res->ai_addr,
                  addr,
                  16);

              // if there was an error we just fail our continuation
              if (result) {
                (*static_cast<decltype(&k)>(data_p->k_p))
                    .Fail(std::string{uv_err_name(result)});
              } else {
                // succeed the resulting ip
                (*static_cast<decltype(&k)>(data_p->k_p))
                    .Start(std::string{addr});
              }
              uv_freeaddrinfo(res);
            }
          };
          auto error = uv_getaddrinfo(
              loop,
              &(data.resolver),
              p_get_addr_info_cb,
              data.addr.c_str(),
              data.port.c_str(),
              &(data.hints));

          // if there was an error we just fail our continuation
          if (error)
            (*static_cast<decltype(&k)>(data.k_p))
                .Fail(std::string{uv_err_name(error)});
        };

        data.loop.Invoke(&data.start);
      });
  // TODO (Artur) !!!!!!!!!!!!!!!
  // Think later about implementing
  // .interrupt([](auto &data, auto &k){...}); callback
}

inline auto DomainNameResolve(
    const std::string& address,
    const std::string& port) {
  return DomainNameResolve(EventLoop::Default(), address, port);
}

} // namespace eventuals
} // namespace stout
