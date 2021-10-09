#include "stout/event-loop.h"
#include "uv.h"

using stout::eventuals::EventLoop;

class HTTPMockServer {
 public:
  void Run(EventLoop& loop, int* port_binded) {
    uv_tcp_init(loop, &server_sockfd);
    uv_tcp_init(loop, &client_sockfd);
    uv_ip4_addr("0.0.0.0", 0, &addr);

    uv_tcp_bind(&server_sockfd, reinterpret_cast<const sockaddr*>(&addr), 0);

    sockaddr_in storage = {};
    int namelen = sizeof(storage);
    uv_tcp_getsockname(
        &server_sockfd,
        reinterpret_cast<sockaddr*>(&storage),
        &namelen);
    *port_binded = htons(storage.sin_port);

    server_sockfd.data = this;
    client_sockfd.data = this;

    uv_listen(
        (uv_stream_t*) &server_sockfd,
        128,
        [](uv_stream_t* server_stream, int status) {
          auto& server_sockfd = *reinterpret_cast<uv_tcp_t*>(server_stream);
          auto& data = *static_cast<HTTPMockServer*>(server_sockfd.data);

          uv_accept(
              server_stream,
              reinterpret_cast<uv_stream_t*>(&data.client_sockfd));

          auto read_cb = [](
                             uv_stream_t* client,
                             ssize_t nread,
                             const uv_buf_t* buf) {
            if (nread > 0) {
              auto& client_sockfd = *reinterpret_cast<uv_tcp_t*>(client);
              auto& data = *static_cast<HTTPMockServer*>(client_sockfd.data);

              std::string http_method(buf->base, 4);

              auto write_cb = [](uv_write_t* req, int status) {
              };

              auto close_cb = [](uv_handle_t* handle) {
              };

              if (http_method == "GET ") {
                data.write_buffer = uv_buf_init(
                    const_cast<char*>(data.GET_response.data()),
                    data.GET_response.size());
                uv_write(
                    &data.write_req,
                    client,
                    &data.write_buffer,
                    1,
                    write_cb);
                uv_close((uv_handle_t*) &data.client_sockfd, close_cb);
                uv_close((uv_handle_t*) &data.server_sockfd, close_cb);
              } else if (http_method == "POST") {
                data.write_buffer = uv_buf_init(
                    const_cast<char*>(data.POST_response.data()),
                    data.POST_response.size());
                uv_write(
                    &data.write_req,
                    client,
                    &data.write_buffer,
                    1,
                    write_cb);
                uv_close((uv_handle_t*) &data.client_sockfd, close_cb);
                uv_close((uv_handle_t*) &data.server_sockfd, close_cb);
              } else {
                uv_close((uv_handle_t*) client, nullptr);
              }
            }
            if (nread < 0) {
              uv_close((uv_handle_t*) client, nullptr);
            }
          };

          uv_read_start(
              reinterpret_cast<uv_stream_t*>(&data.client_sockfd),
              [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
                auto& client_sockfd = *reinterpret_cast<uv_tcp_t*>(handle);
                auto& data = *static_cast<HTTPMockServer*>(client_sockfd.data);

                data.read_base.resize(suggested_size);
                buf->base = const_cast<char*>(data.read_base.data());
                buf->len = suggested_size;
              },
              read_cb);
        });
  }

  const std::string GET_response =
      "HTTP/1.1 200 OK\n"
      "Version: HTTP/1.1\n"
      "Content-Type: text/html; charset=utf-8\n"
      "Content-Length: 13\n\n"
      "<html></html>";

  const std::string POST_response =
      "HTTP/1.1 201 Created\n"
      "Version: HTTP/1.1\n"
      "Content-Type: application/json; charset=utf-8\n"
      "Content-Length: 55\n\n"
      "{\n"
      "  \"body\": \"message\",\n"
      "  \"title\": \"test\",\n"
      "  \"id\": 101\n"
      "}";

  uv_tcp_t server_sockfd = {};
  uv_tcp_t client_sockfd = {};
  sockaddr_in addr = {};

  uv_buf_t write_buffer = {};
  std::string read_base = "";

  uv_write_t write_req = {};
};