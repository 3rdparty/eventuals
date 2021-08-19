#pragma once

#include <memory>

#include "stout/eventual.h"
#include "stout/libuv/loop.h"
#include "uv.h"

namespace stout {
namespace uv {
namespace ip {

using stout::uv::Loop;

auto Resolve(
    Loop& loop,
    const std::string& address,
    const std::string& port) {
  struct Data {
    addrinfo hints;
    uv_getaddrinfo_t resolver;
    Data() {
      hints.ai_family = PF_INET;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
      hints.ai_flags = 0;
    }
  };

  return stout::eventuals::Eventual<std::string>()
      .context(Data())
      .start([&](auto& data, auto& k) {
        data.resolver.data = &k; // we should store our continuation k
        // in the field data(void*), because we can't use
        // capturing lambdas as callbacks for libuv!
        // the link down will be helpfull:
        // https://misfra.me/2016/02/24/libuv-and-cpp/


        //callback
        auto p_get_addr_info_cb = [](uv_getaddrinfo_t* req,
                                     int status,
                                     struct addrinfo* res) {
          if (status < 0) {
            stout::eventuals::fail(
                *static_cast<decltype(&k)>(req->data),
                std::string{uv_err_name(status)});
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
              stout::eventuals::fail(
                  *static_cast<decltype(&k)>(req->data),
                  std::string{uv_err_name(result)});
            } else {
              // succeed the resulting ip
              stout::eventuals::succeed(
                  *static_cast<decltype(&k)>(req->data),
                  std::string{addr});
            }
          }
        };

        auto result = uv_getaddrinfo(
            loop,
            &(data.resolver),
            p_get_addr_info_cb,
            address.c_str(),
            port.c_str(),
            &(data.hints));

        // if there was an error we just fail our continuation
        if (result)
          stout::eventuals::fail(k, std::string{uv_err_name(result)});
      });
}

} // namespace ip
} // namespace uv
} // namespace stout
