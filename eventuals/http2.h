#pragma once

#include <filesystem>

#include "curl/curl.h"
#include "eventuals/event-loop.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace http {

////////////////////////////////////////////////////////////////////////

struct Response {
  long code;
  std::string body;
  // TODO(folming): transform to Headers type:
  // using Header = std::pair<std::string, std::string>;
  // using Headers = std::vector<Header>;
  std::string headers;
};

////////////////////////////////////////////////////////////////////////

enum class Method {
  GET,
  POST,
};

////////////////////////////////////////////////////////////////////////

template <
    typename URIType_,
    typename MethodType_,
    typename HeadersType_,
    typename BodyType_,
    typename TimeoutType_,
    typename CAPathType_>
struct Request {
  struct Builder {
  };

  // std::string
  URIType_ uri_;
  // Method
  MethodType_ method_;
  // std::vector<std::pair<std::string, std::string>>
  HeadersType_ headers_;
  // std::pair<const void*, size_t>
  BodyType_ body_;
  // std::chrono::nanoseconds
  TimeoutType_ timeout_;
  // std::filesystem::path
  CAPathType_ ca_path_;
};

////////////////////////////////////////////////////////////////////////

class Client;

////////////////////////////////////////////////////////////////////////

// Our own eventual for using libcurl with the EventLoop.
//
// The general algorithm:
// 1. Create easy and multi handles. Set options for them.
//    Add easy handle to multi handle with curl_multi_add_handle.
//    TIMERFUNCTION is called to set a timer which will tell us when
//    to perform checks on libcurl handles.
//    SOCKETFUNCTION is called by using curl_multi_socket_action.
//    We can pass specific socket descriptor to work with that particular
//    socket or we can pass CURL_SOCKET_TIMEOUT to let libcurl call a function
//    for each socket that is currently in use.
// 2. Whenever SOCKETFUNCTION is called we check for events and set a poll
//    handle for the particular socket. This poll handle is created on heap
//    and that's why we put its pointer inside vector so that we can stop it
//    when we have to interrupt the transfer.
// 3. Whenever curl_multi_socket_action is called we can get an amount of
//    remaining running easy handles. If this value is 0 then we read info
//    from multi handle using check_multi_info lambda and clean everything up.
struct _HTTPEventual {
  template <typename K_, typename RequestType_>
  struct Continuation {
    Continuation(
        K_&& k,
        EventLoop& loop,
        Client& client,
        RequestType_&& request)
      : k_(std::move(k)),
        loop_(loop),
        request_(std::move(request)),
        easy_(curl_easy_init(), &curl_easy_cleanup),
        multi_(curl_multi_init(), &curl_multi_cleanup),
        start_(&loop_, "HTTP (start)"),
        interrupt_(&loop_, "HTTP (interrupt)") {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        loop_(that.loop_),
        request_(std::move(that.request_)),
        easy_(std::move(that.easy_)),
        multi_(std::move(that.multi_)),
        start_(&that.loop_, "HTTP (start)"),
        interrupt_(&that.loop_, "HTTP (interrupt)") {
      CHECK(!that.started_ || !that.completed_) << "moving after starting";
      CHECK(!handler_);
    }

    ~Continuation() {
      CHECK(!started_ || timer_closed_);
    }

    void Start() {
      CHECK(!started_ && !completed_);

      loop_.Submit(
          [this]() {
            if (!completed_) {
              started_ = true;

              // Initial curl ptr checks.
              if (easy_ == nullptr) {
                completed_ = true;
                k_.Fail(error_bad_alloc_easy_handle_);
                return;
              }

              if (multi_ == nullptr) {
                completed_ = true;
                k_.Fail(error_bad_alloc_multi_handle_);
                return;
              }

              // Callbacks.
              // Called only once - finishes the transfer.
              static auto check_multi_info = [](Continuation& continuation) {
                continuation.completed_ = true;

                // Stores the amount of remaining messages in multi handle.
                // Unused.
                int msgq = 0;
                CURLMsg* message = curl_multi_info_read(
                    continuation.multi_.get(),
                    &msgq);

                // Getting the response code and body.
                if (message->data.result == CURLE_OK) {
                  // Success.
                  curl_easy_getinfo(
                      continuation.easy_.get(),
                      CURLINFO_RESPONSE_CODE,
                      &continuation.response_code_);
                } else {
                  // Failure.
                  continuation.error_ = message->data.result;
                }

                // Stop transfer completely.
                CHECK_EQ(
                    curl_multi_remove_handle(
                        continuation.multi_.get(),
                        message->easy_handle),
                    CURLM_OK);

                // Memory cleanup.
                for (auto& poll : continuation.polls_) {
                  if (uv_is_active((uv_handle_t*) poll)) {
                    uv_poll_stop(poll);
                  }
                  uv_close(
                      (uv_handle_t*) poll,
                      [](uv_handle_t* handle) {
                        delete handle;
                      });
                }

                continuation.polls_.clear();

                // We don't have to check uv_is_active for timer since
                // libuv checks it by itself.
                // Return value is always 0.
                uv_timer_stop(&continuation.timer_);
                uv_close(
                    (uv_handle_t*) &continuation.timer_,
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.timer_closed_ = true;

                      if (!continuation.error_) {
                        continuation.k_.Start(Response{
                            continuation.response_code_,
                            continuation.response_buffer_.Extract(),
                            continuation.response_headers_buffer_.Extract()});
                      } else {
                        continuation.k_.Fail(
                            curl_easy_strerror(
                                (CURLcode) continuation.error_));
                      }
                    });
              };

              static auto poll_callback = [](uv_poll_t* handle,
                                             int status,
                                             int events) {
                auto& continuation = *(Continuation*) handle->data;

                int flags = 0;
                if (status < 0) {
                  flags = CURL_CSELECT_ERR;
                }
                if (status == 0 && (events & UV_READABLE)) {
                  flags |= CURL_CSELECT_IN;
                }
                if (status == 0 && (events & UV_WRITABLE)) {
                  flags |= CURL_CSELECT_OUT;
                }

                // Getting underlying socket desriptor from poll handle.
                uv_os_fd_t socket_descriptor;
                uv_fileno(
                    (uv_handle_t*) handle,
                    &socket_descriptor);

                // Stores the amount of running easy handles.
                // Set by curl_multi_socket_action.
                int running_handles = 0;

                // Perform an action for the particular socket
                // which is the one we are currently working with.
                // We don't want to perform an action on every socket inside
                // libcurl - only that one.
                curl_multi_socket_action(
                    continuation.multi_.get(),
                    (curl_socket_t) socket_descriptor,
                    flags,
                    &running_handles);

                // If 0 - finalize the transfer.
                if (running_handles == 0) {
                  check_multi_info(continuation);
                }
              };

              static auto timer_callback = [](uv_timer_t* handle) {
                auto& continuation = *(Continuation*) handle->data;

                // Stores the amount of running easy handles.
                // Set by curl_multi_socket_action.
                int running_handles = 0;

                // Called with CURL_SOCKET_TIMEOUT to
                // perform an action with each and every socket
                // currently in use by libcurl.
                curl_multi_socket_action(
                    continuation.multi_.get(),
                    CURL_SOCKET_TIMEOUT,
                    0,
                    &running_handles);

                // If 0 - finalize the transfer.
                if (running_handles == 0) {
                  check_multi_info(continuation);
                }
              };

              static auto socket_function = +[](CURL* easy,
                                                curl_socket_t sockfd,
                                                int what,
                                                Continuation* continuation,
                                                void* socket_poller) {
                int events = 0;

                switch (what) {
                  case CURL_POLL_IN:
                  case CURL_POLL_OUT:
                  case CURL_POLL_INOUT:
                    // Add poll handle for this particular socket.
                    if (what & CURL_POLL_IN) {
                      events |= UV_READABLE;
                    }
                    if (what & CURL_POLL_OUT) {
                      events |= UV_WRITABLE;
                    }

                    // If no poll handle is assigned to this socket.
                    if (socket_poller == nullptr) {
                      socket_poller = new uv_poll_t();
                      continuation->polls_.push_back(
                          (uv_poll_t*) socket_poller);

                      CHECK_EQ(
                          uv_poll_init_socket(
                              continuation->loop_,
                              (uv_poll_t*) socket_poller,
                              sockfd),
                          0);

                      uv_handle_set_data(
                          (uv_handle_t*) socket_poller,
                          continuation);

                      // Assign created poll handle so that in the future
                      // we can get it through socket_poller argument.
                      // Useful to check if we already have a poll handle
                      // for the socket currently in use.
                      CHECK_EQ(
                          curl_multi_assign(
                              continuation->multi_.get(),
                              sockfd,
                              socket_poller),
                          CURLM_OK);
                    }

                    // Stops poll handle if it was started.
                    if (uv_is_active((uv_handle_t*) socket_poller)) {
                      CHECK_EQ(
                          uv_poll_stop(
                              (uv_poll_t*) socket_poller),
                          0);
                    }

                    CHECK_EQ(
                        uv_poll_start(
                            (uv_poll_t*) socket_poller,
                            events,
                            poll_callback),
                        0);

                    break;
                  case CURL_POLL_REMOVE:
                    // Remove poll handle for this particular socket.
                    uv_poll_stop((uv_poll_t*) socket_poller);
                    uv_close(
                        (uv_handle_t*) socket_poller,
                        [](uv_handle_t* handle) {
                          delete handle;
                        });

                    // Remove this poll handle from vector.
                    for (auto it = continuation->polls_.begin();
                         it != continuation->polls_.end();
                         it++) {
                      if (*it == (uv_poll_t*) socket_poller) {
                        continuation->polls_.erase(it);
                        break;
                      }
                    }

                    // Remove assignment of poll handle to this socket.
                    CHECK_EQ(
                        curl_multi_assign(
                            continuation->multi_.get(),
                            sockfd,
                            nullptr),
                        CURLM_OK);
                    break;
                }
              };

              // Used by libcurl to set a timer after
              // which we should start checking handles inside libcurl.
              static auto timer_function = +[](CURLM* multi,
                                               long timeout_ms,
                                               Continuation* continuation) {
                if (timeout_ms < 0) {
                  timeout_ms = 0;
                }

                uv_timer_start(
                    &continuation->timer_,
                    timer_callback,
                    timeout_ms,
                    0);
              };

              // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
              static auto write_function = +[](char* data,
                                               size_t size,
                                               size_t nmemb,
                                               Continuation* continuation) {
                continuation->response_buffer_ += std::string(
                    data,
                    size * nmemb);

                return nmemb * size;
              };

              // https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html
              static auto header_function = +[](char* data,
                                                size_t size,
                                                size_t nmemb,
                                                Continuation* continuation) {
                continuation->response_headers_buffer_ += std::string(
                    data,
                    size * nmemb);

                return nmemb * size;
              };

              // CURL multi options.
              if (CURLMcode error_code = curl_multi_setopt(
                      multi_.get(),
                      CURLMOPT_SOCKETDATA,
                      this);
                  error_code != CURLM_OK) {
                completed_ = true;
                k_.Fail(curl_multi_strerror(error_code));
                return;
              }

              if (CURLMcode error_code = curl_multi_setopt(
                      multi_.get(),
                      CURLMOPT_SOCKETFUNCTION,
                      socket_function);
                  error_code != CURLM_OK) {
                completed_ = true;
                k_.Fail(curl_multi_strerror(error_code));
                return;
              }

              if (CURLMcode error_code = curl_multi_setopt(
                      multi_.get(),
                      CURLMOPT_TIMERDATA,
                      this);
                  error_code != CURLM_OK) {
                completed_ = true;
                k_.Fail(curl_multi_strerror(error_code));
                return;
              }

              if (CURLMcode error_code = curl_multi_setopt(
                      multi_.get(),
                      CURLMOPT_TIMERFUNCTION,
                      timer_function);
                  error_code != CURLM_OK) {
                completed_ = true;
                k_.Fail(curl_multi_strerror(error_code));
                return;
              }

              // CURL easy options.
              // URI.
              if constexpr (IsUndefined<decltype(request_.uri_)>::value) {
                completed_ = true;
                k_.Fail(error_no_uri_);
                return;
              } else {
                if (CURLcode error_code = curl_easy_setopt(
                        easy_.get(),
                        CURLOPT_URL,
                        request_.uri_.c_str());
                    error_code != CURLE_OK) {
                  completed_ = true;
                  k_.Fail(curl_easy_strerror(error_code));
                  return;
                }
              }

              // Method.
              if constexpr (IsUndefined<decltype(request_.method_)>::value) {
                completed_ = true;
                k_.Fail(error_no_method_);
                return;
              } else {
                switch (request_.method_) {
                  case Method::GET:
                    if (CURLcode error_code = curl_easy_setopt(
                            easy_.get(),
                            CURLOPT_HTTPGET,
                            1);
                        error_code != CURLE_OK) {
                      completed_ = true;
                      k_.Fail(curl_easy_strerror(error_code));
                      return;
                    }
                    break;
                  case Method::POST:
                    if (CURLcode error_code = curl_easy_setopt(
                            easy_.get(),
                            CURLOPT_HTTPPOST,
                            1);
                        error_code != CURLE_OK) {
                      completed_ = true;
                      k_.Fail(curl_easy_strerror(error_code));
                      return;
                    }
                    break;
                  default:
                    completed_ = true;
                    k_.Fail(error_unknown_method_);
                    return;
                }
              }

              // Body.
              if constexpr (!IsUndefined<decltype(request_.body_)>::value) {
                if (CURLcode error_code = curl_easy_setopt(
                        easy_.get(),
                        CURLOPT_POSTFIELDS,
                        request_.body_.first);
                    error_code != CURLE_OK) {
                  completed_ = true;
                  k_.Fail(curl_easy_strerror(error_code));
                  return;
                }

                if (CURLcode error_code = curl_easy_setopt(
                        easy_.get(),
                        CURLOPT_POSTFIELDSIZE_LARGE,
                        request_.body_.second);
                    error_code != CURLE_OK) {
                  completed_ = true;
                  k_.Fail(curl_easy_strerror(error_code));
                  return;
                }
              }

              // Pass 'this' to write_function lambda.
              if (CURLcode error_code = curl_easy_setopt(
                      easy_.get(),
                      CURLOPT_WRITEDATA,
                      this);
                  error_code != CURLE_OK) {
                completed_ = true;
                k_.Fail(curl_easy_strerror(error_code));
                return;
              }

              // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
              if (CURLcode error_code = curl_easy_setopt(
                      easy_.get(),
                      CURLOPT_WRITEFUNCTION,
                      write_function);
                  error_code != CURLE_OK) {
                completed_ = true;
                k_.Fail(curl_easy_strerror(error_code));
                return;
              }

              // Pass 'this' to header_function lambda.
              if (CURLcode error_code = curl_easy_setopt(
                      easy_.get(),
                      CURLOPT_HEADERDATA,
                      this);
                  error_code != CURLE_OK) {
                completed_ = true;
                k_.Fail(curl_easy_strerror(error_code));
                return;
              }

              // https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html
              if (CURLcode error_code = curl_easy_setopt(
                      easy_.get(),
                      CURLOPT_HEADERFUNCTION,
                      header_function);
                  error_code != CURLE_OK) {
                completed_ = true;
                k_.Fail(curl_easy_strerror(error_code));
                return;
              }

              // The internal mechanism of libcurl to provide timeout support.
              // Not accurate at very low values.
              // 0 means that transfer can run indefinitely.
              if constexpr (IsUndefined<decltype(request_.timeout_)>::value) {
                if (CURLcode error_code = curl_easy_setopt(
                        easy_.get(),
                        CURLOPT_TIMEOUT_MS,
                        0);
                    error_code != CURLE_OK) {
                  completed_ = true;
                  k_.Fail(curl_easy_strerror(error_code));
                  return;
                }
              } else {
                if (request_.timeout_.count() < 0) {
                  completed_ = true;
                  k_.Fail(error_negative_timeout_);
                  return;
                } else {
                  if (CURLcode error_code = curl_easy_setopt(
                          easy_.get(),
                          CURLOPT_TIMEOUT_MS,
                          std::chrono::duration_cast<
                              std::chrono::milliseconds>(
                              request_.timeout_));
                      error_code != CURLE_OK) {
                    completed_ = true;
                    k_.Fail(curl_easy_strerror(error_code));
                    return;
                  }
                }
              }

              // Initializing timer.
              if (int error_code = uv_timer_init(loop_, &timer_);
                  error_code != 0) {
                completed_ = true;
                k_.Fail(uv_strerror(error_code));
                return;
              }
              uv_handle_set_data(
                  reinterpret_cast<uv_handle_t*>(&timer_),
                  this);

              // Start handling connection.
              CHECK_EQ(
                  curl_multi_add_handle(
                      multi_.get(),
                      easy_.get()),
                  CURLM_OK);
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

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      handler_.emplace(&interrupt, [this]() {
        loop_.Submit(
            [this]() {
              if (!started_) {
                CHECK(!completed_ && !error_);
                completed_ = true;
                k_.Stop();
              } else if (!completed_) {
                CHECK(started_ && !error_);
                completed_ = true;

                for (auto& poll : polls_) {
                  if (uv_is_active((uv_handle_t*) poll)) {
                    uv_poll_stop(poll);
                  }
                  uv_close(
                      (uv_handle_t*) poll,
                      [](uv_handle_t* handle) {
                        delete handle;
                      });
                }
                polls_.clear();

                // We don't have to check uv_is_active for timer since
                // libuv checks it by itself.
                // Return value is always 0.
                uv_timer_stop(&timer_);
                uv_close(
                    (uv_handle_t*) &timer_,
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.timer_closed_ = true;

                      continuation.k_.Stop();
                    });

                CHECK_EQ(
                    curl_multi_remove_handle(
                        multi_.get(),
                        easy_.get()),
                    CURLM_OK);
              }
            },
            &interrupt_);
      });

      // NOTE: we always install the handler in case 'Start()'
      // never gets called.
      handler_->Install();
    }

   private:
    K_ k_;
    EventLoop& loop_;
    RequestType_ request_;

    // CURL internals.
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy_;
    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi_;
    uv_timer_t timer_;
    std::vector<uv_poll_t*> polls_;

    // Response variables.
    CURLcode error_ = CURLE_OK;
    long response_code_ = 0;
    EventLoop::Buffer response_buffer_;
    EventLoop::Buffer response_headers_buffer_;

    bool started_ = false;
    bool completed_ = false;
    bool timer_closed_ = true;

    EventLoop::Waiter start_;
    EventLoop::Waiter interrupt_;

    std::optional<Interrupt::Handler> handler_;

    // Error strings.
    // NOTE: using const char* because constexpr std::string
    // is only supported since C++20.
    constexpr static const char* error_bad_alloc_easy_handle_ =
        "Internal CURL error: wasn't able to allocate easy handle.";
    constexpr static const char* error_bad_alloc_multi_handle_ =
        "Internal CURL error: wasn't able to allocate multi handle.";
    constexpr static const char* error_no_uri_ =
        "No uri set.";
    constexpr static const char* error_no_method_ =
        "No method was set for this request.";
    constexpr static const char* error_unknown_method_ =
        "Unknown HTTP method.";
    constexpr static const char* error_negative_timeout_ =
        "Timeout can't have a negative value.";
  };

  template <typename RequestType>
  struct Composable {
    template <typename>
    using ValueFrom = Response;

    template <typename Args, typename K>
    auto k(K k) && {
      return Continuation<K, RequestType>{
          std::move(k),
          loop_,
          client_,
          std::move(request_)};
    }

    EventLoop& loop_;
    Client& client_;
    RequestType request_;
  };
};

////////////////////////////////////////////////////////////////////////

class Client {
 public:
  Client(EventLoop& loop)
    : loop_(loop) {}

  //Client(const Client& that) {}
  //Client(Client&& that) {}
  //
  //Client& operator=(const Client& that) {
  //  return *this;
  //}
  //
  //Client& operator=(Client&& that) {
  //  return *this;
  //}

  // NOTE (from folming to benh): Go library uses 'Do' instead of 'Send'.
  template <typename RequestType>
  auto Do(const RequestType& request) {
    // TODO(benh): borrow 'this' so HTTPEventual can't outlive a Client.
    return RescheduleAfter(_HTTPEventual::Composable<RequestType>{
        loop_,
        *this,
        request});
  }

  static Client& Default();

  // TODO: Insert various client options here...

 private:
  EventLoop& loop_;
};

////////////////////////////////////////////////////////////////////////

inline auto Get(EventLoop& loop, const std::string& uri) {
  Request<
      std::string,
      Method,
      Undefined,
      Undefined,
      Undefined,
      Undefined>
      request{uri, Method::GET};
  return Client::Default().Do(request);
}

////////////////////////////////////////////////////////////////////////

inline auto Get(const std::string& uri) {
  return Get(EventLoop::Default(), uri);
}

////////////////////////////////////////////////////////////////////////

inline auto Post(
    EventLoop& loop,
    const std::string& uri,
    const std::string& content_type,
    const void* body,
    size_t body_size) {
  std::vector<std::pair<std::string, std::string>> headers;
  headers.push_back({"Content-Type", content_type});

  Request<
      std::string,
      Method,
      std::vector<std::pair<std::string, std::string>>,
      std::pair<const void*, size_t>,
      Undefined,
      Undefined>
      request{
          uri,
          Method::POST,
          headers,
          {body, body_size}};
  return Client::Default().Do(request);
}

////////////////////////////////////////////////////////////////////////

inline auto Post(
    const std::string& uri,
    const std::string& content_type,
    const void* body,
    size_t body_size) {
  return Post(EventLoop::Default(), uri, content_type, body, body_size);
}

////////////////////////////////////////////////////////////////////////

} // namespace http

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
