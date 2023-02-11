#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

#include "eventuals/closure.h"
#include "eventuals/finally.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"
#include "eventuals/unbuffered-pipe.h"
#include "stout/borrowable.h"


////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Value_, typename Errors_>
struct PromiseData {
  PromiseData(std::string&& name)
    : name(std::move(name)) {}

  PromiseData(PromiseData&& that) = delete;

  std::optional<Scheduler::Context> context;

  std::string name;

  // NOTE: we use an 'UnbufferedPipe' instead of a 'Notification'
  // because the former provides a "barrier" like semantics where we
  // can ensure that the promise has completed executing it's eventual
  // and thus we know we can destruct it's data.
  UnbufferedPipe<std::monostate> pipe;

  std::optional<
      std::conditional_t<
          std::is_void_v<Value_>,
          Undefined,
          std::conditional_t<
              !std::is_reference_v<Value_>,
              Value_,
              std::reference_wrapper<
                  std::remove_reference_t<Value_>>>>>
      value;

  // NOTE: we introduce a special "stopped" type here so as not to
  // conflict with 'eventuals::Stopped'.
  struct Stopped {};

  using Variant = apply_tuple_types_t<
      std::variant,
      tuple_types_union_t<
          std::tuple<Stopped, std::exception_ptr>,
          Errors_>>;

  std::optional<Variant> error;

  Interrupt interrupt;

  std::atomic<bool> started = false;
  std::atomic<bool> finished = false;
};


template <typename Value_, typename... Errors_>
class Future {
 public:
  Future(Future&& that) = default;

  auto Wait() {
    return Closure([data = Reborrow(data_)]() {
      // NOTE: using an 'UnbufferedPipe' allows us to rendezvous with
      // the promise so that we know after we've returned from
      // 'Write()' that the promise has executed it's terminal
      // continuation.
      DLOG(INFO) << Scheduler::Context::Get()->name() << " DOING WRITE " << data->name << " " << &data->pipe;
      return data->pipe.Write(std::monostate{})
          >> Then([&]() {
               DLOG(INFO) << Scheduler::Context::Get()->name() << " DOING CLOSE " << data->name << " " << &data->pipe;
             })
          >> data->pipe.Close();
    });
  }

  // NOTE: 'Get()' does a 'std::move()' of the value or error so it
  // can only be called once which we capture by requiring an rvalue
  // reference for 'this' when invoking.
  auto Get() && {
    return Closure([data = Reborrow(data_)]() mutable {
      // NOTE: see comment in 'Wait()' for why we do both 'Write()'
      // and 'Close()'. We don't call 'Wait()' to avoid borrowing
      // 'data' twice.
      DLOG(INFO) << Scheduler::Context::Get()->name() << " DOING WRITE " << data->name << " " << &data->pipe;
      return data->pipe.Write(std::monostate{})
          >> Then([&]() {
               DLOG(INFO) << Scheduler::Context::Get()->name() << " DOING CLOSE " << data->name << " " << &data->pipe;
             })
          >> data->pipe.Close()
          >> Eventual<Value_>()
                 .template raises<Errors_...>()
                 .start([&](auto& k) {
                   if (data->value) {
                     if constexpr (std::is_void_v<Value_>) {
                       k.Start();
                     } else {
                       k.Start(std::move(data->value.value()));
                     }
                   } else {
                     CHECK(data->error);
                     std::visit(
                         [&](auto&& error) {
                           if constexpr (
                               std::is_same_v<
                                   typename PromiseData<
                                       Value_,
                                       std::tuple<Errors_...>>::Stopped,
                                   std::decay_t<decltype(error)>>) {
                             k.Stop();
                           } else {
                             k.Fail(std::forward<decltype(error)>(error));
                           }
                         },
                         std::move(data->error.value()));
                   }
                 })
                 .fail([](auto& k, auto&& error) {
                   static_assert(
                       always_false_v<decltype(k)>,
                       "Unreachable");
                 })
                 .stop([](auto& k) {
                   // TODO(benh): remove this 'static_assert' once
                   // 'Notification' has interrupt support.
                   static_assert(
                       always_false_v<decltype(k)>,
                       "Unreachable");
                   k.Stop();
                 });
    });
  }

  void Interrupt() {
    data_->interrupt.Trigger();
  }

  const std::string& name() const {
    return data_->name;
  }

 private:
  template <typename F>
  friend auto Promise(std::string&&, F);

  Future(stout::borrowed_ref<
         PromiseData<Value_, std::tuple<Errors_...>>>&& data)
    : data_(std::move(data)) {}

  stout::borrowed_ref<PromiseData<Value_, std::tuple<Errors_...>>> data_;
};


template <typename F>
[[nodiscard]] auto Promise(std::string&& name, F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Promise' expects a callable (e.g., a lamdba) not an eventual");

  static_assert(
      std::is_invocable_v<F>,
      "'Promise' expects a callable (e.g., a lambda) that takes no arguments");

  using E = std::invoke_result_t<F>;

  // TODO(benh): support propagating a possible value into the
  // eventual returned by 'f' rather than assuming no value will be
  // propagated. Currently we require 'void' being passed in so we can
  // determine 'Errors'.
  using Value = typename E::template ValueFrom<void>;
  using Errors = typename E::template ErrorsFrom<void, std::tuple<>>;

  // Helper struct for ensuring we've waited for the promise to
  // finish, see usage below.
  struct DestructionChecker {
    DestructionChecker(PromiseData<Value, Errors>& data)
      : data(&data) {}

    DestructionChecker(DestructionChecker&& that) {
      CHECK_EQ(data, nullptr) << "moving after starting";
      data = that.data;
      that.data = nullptr;
    }

    ~DestructionChecker() {
      if (data != nullptr) {
        CHECK_EQ(data->started.load(), data->finished.load())
            << "context " << data->name << " has not finished; "
            << "did you forget to 'Wait()' or 'Get()' the future?";
      }
    }

    PromiseData<Value, Errors>* data = nullptr;
  };

  return Then([f = std::move(f),
               data = Lazy<
                   stout::Borrowable<
                       PromiseData<Value, Errors>>>(
                   std::move(name))]() mutable {
    auto k = Build(
        Then([&, checker = DestructionChecker(**data)]() {
          (*data)->started.store(true);
        })
        // NOTE: using 'Then' here to defer invoking 'f' until we
        // have the correct scheduler context.
        >> Then(std::move(f))
        >> Eventual<void>()
               .start([&data](auto& k, auto&&... v) {
                 if constexpr (std::is_void_v<Value>) {
                   (*data)->value.emplace(Undefined());
                 } else {
                   (*data)->value.emplace(
                       std::forward<decltype(v)>(v)...);
                 }
                 k.Start();
               })
               .fail([&data](auto& k, auto&& e) {
                 (*data)->error.emplace(
                     typename PromiseData<Value, Errors>::Variant(
                         std::forward<decltype(e)>(e)));
                 k.Start();
               })
               .stop([&data](auto& k) {
                 (*data)->error.emplace(
                     typename PromiseData<Value, Errors>::Variant(
                         typename PromiseData<
                             Value,
                             Errors>::Stopped()));
                 k.Start();
               })
        // TODO(benh): ensure this is uninterruptible.
        >> Then([&]() {
            DLOG(INFO) << Scheduler::Context::Get()->name() << " DOING READ " << (*data)->name << " " << &((*data)->pipe);
            // NOTE: need to use 'Preempt()' to ensure that when
            // 'Future::Wait()' or 'Future::Get()' rendezvous with us
            // we won't defer execution but will execute the terminal
            // immediately so that we can safely destruct the promise!
            static std::atomic<int> i = 0;
            return Preempt(
                "[Promise - \"rendezvous\" " + std::to_string(i++) + "]",
                (*data)->pipe.Read()
                    >> Head());
          })
        >> Terminal()
               .start([&](std::monostate&&) {
                 DLOG(INFO) << Scheduler::Context::Get()->name() << " FINISHED " << (*data)->name << " " << &((*data)->pipe);
                 (*data)->finished.store(true);
               })
               .fail([](std::runtime_error&& error) {
                 LOG(FATAL)
                     << "Rendezvous with future should never fail: "
                     << error.what();
               })
               .stop([](auto&& unreachable) {
                 static_assert(
                     always_false_v<decltype(unreachable)>,
                     "Unreachable");
               }));

    return Then([&, k = std::move(k)]() mutable {
      // Clone the current scheduler context for running the eventual.
      (*data)->context.emplace(
          Scheduler::Context::Get()->name() + " " + (*data)->name);

      (*data)->context->scheduler()->Submit(
          [&]() {
            CHECK_EQ(
                &(*data)->context.value(),
                Scheduler::Context::Get().get());
            k.Register((*data)->interrupt);
            k.Start();
          },
          (*data)->context.value());

      return apply_tuple_types_t<
          Future,
          tuple_types_concatenate_t<
              std::tuple<Value>,
              Errors>>(Borrow(*data));
    });
  });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
