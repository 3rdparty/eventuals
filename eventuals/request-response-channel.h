#pragma once

#include <deque>

#include "eventuals/callback.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Request_, typename Response_>
class RequestResponseChannel final : public Synchronizable {
 public:
  RequestResponseChannel()
    : has_requests_or_shutdown_(&lock()),
      has_responses_or_shutdown_(&lock()) {}

  ~RequestResponseChannel() override = default;

  [[nodiscard]] auto Request(Request_&& request) {
    return Synchronized(
        Then([this, request = std::move(request)]() mutable {
          if (!shutdown_) {
            requests_.emplace_back(std::move(request));
            has_requests_or_shutdown_.Notify();
          }

          return has_responses_or_shutdown_.Wait([this]() {
            return responses_.empty() && !shutdown_;
          });
        })
        | Then([this]() {
            if (!responses_.empty()) {
              auto response = std::move(responses_.front());
              responses_.pop_front();
              return std::make_optional(std::move(response));
            } else {
              CHECK(shutdown_);
              return std::optional<Response_>();
            }
          }));
  }

  [[nodiscard]] auto Respond(Response_&& response) {
    return Synchronized(
        Then([this, response = std::move(response)]() mutable {
          if (!shutdown_) {
            responses_.emplace_back(std::move(response));
            has_responses_or_shutdown_.Notify();
          }
        }));
  }

  [[nodiscard]] auto RespondBatch(std::deque<Response_>&& responses) {
    return Synchronized(
        Then([this, responses = std::move(responses)]() mutable {
          // TODO(benh): raise an error if already shutdown?
          if (!shutdown_) {
            for (auto& response : responses) {
              responses_.emplace_back(std::move(response));
            }
            has_responses_or_shutdown_.Notify();
          }
        }));
  }

  [[nodiscard]] auto Read() {
    return Repeat()
        | Synchronized(
               Map([this]() {
                 return has_requests_or_shutdown_.Wait([this]() {
                   return requests_.empty() && !shutdown_;
                 });
               })
               | Map([this]() {
                   if (!requests_.empty()) {
                     auto request = std::move(requests_.front());
                     requests_.pop_front();
                     return std::make_optional(std::move(request));
                   } else {
                     CHECK(shutdown_);
                     return std::optional<Request_>();
                   }
                 }))
        | Until([](auto& request) {
             return !request.has_value();
           })
        | Map([](auto&& request) {
             CHECK(request);
             // NOTE: need to use 'Just' here in case 'T' is an
             // eventual otherwise we'll try and compose with it here!
             return Just(std::move(*request));
           });
  }

  // Returns an eventual 'std::optional<std::deque<Request_>>' where
  // no value implies there are no more requests.
  [[nodiscard]] auto ReadBatch() {
    return Synchronized(
        Then([this]() {
          return has_requests_or_shutdown_.Wait([this]() {
            return requests_.empty() && !shutdown_;
          });
        })
        | Then([this]() {
            if (!requests_.empty()) {
              std::deque<Request_> requests = std::move(requests_);
              requests_.clear();
              return std::make_optional(requests);
            } else {
              CHECK(shutdown_);
              return std::optional<std::deque<Request_>()>();
            }
          }));
  }

  // Shutdown the channel for any more requests or responses.
  [[nodiscard]] auto Shutdown() {
    return Synchronized(Then([this]() {
      // TODO(benh): raise an error if already shutdown?
      shutdown_ = true;
      has_requests_or_shutdown_.Notify();
      has_responses_or_shutdown_.Notify();
    }));
  }

 private:
  ConditionVariable has_requests_or_shutdown_;
  ConditionVariable has_responses_or_shutdown_;
  std::deque<Request_> requests_;
  std::deque<Response_> responses_;
  bool shutdown_ = false;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
