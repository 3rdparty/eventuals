#pragma once

#include <deque>
#include <iostream>
#include <mutex>
#include <optional>

#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/until.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Uses the eventual returned from calling the specified function 'f'
// to handle each value in the stream concurrently.
//
// Concurrent here means that every eventual returned from calling 'f'
// will have it's own scheduler context but it will use the default
// scheduler which is preemptive, i.e., no threads or other execution
// resources will be used. We call each eventual with it's own
// scheduling context a "fiber".
//
// The eventual returned from calling 'f' should be a generator, i.e.,
// it can compose with an "upstream" stream and itself is a stream
// (for example, it should be a 'Map()' or 'FlatMap()').
//
// If one of the eventuals raises a failure or stops, then we attempt
// to call "done" on the upstream stream, wait for all of the
// eventuals to finish, and then propagate the failure or stop
// downstream. There is one caveat to this case which is that we can't
// attempt to tell the upstream we're done until it has called down
// into us which means if we've called 'Next()' and it hasn't returned
// then we could wait forever. For now, the way to rectify this
// situation is to make sure that you interrupt the upstream stream
// you are composing with (in the future we'll be adding something
// like 'TypeErasedStream::Interrupt()' to support this case directly.
template <typename F>
[[nodiscard]] auto Concurrent(F f);

////////////////////////////////////////////////////////////////////////

struct _Concurrent final {
  // 'TypeErasedAdaptor' is used to type erase all of the adaptor
  // "functionality" so that the compiler doesn't have to instantiate
  // more functions than necessary (even if the compiler should be
  // able to figure this out, emperically this doesn't appear to be
  // the case, at least with clang).
  struct TypeErasedAdaptor : public Synchronizable {
    // 'TypeErasedFiber' is used to type erase all fiber functionality
    // that can be used by 'TypeErasedAdaptor'.
    //
    // A "fiber" is a scheduling context and a continuation. The
    // continuation is stored in 'Adaptor::Fiber' below because it
    // requires template types.
    //
    // Each fiber is part of a linked list of fibers that gets pruned
    // or appended to during runtime as new fibers are needed. We use
    // a linked list rather than an existing collection because it
    // nicely matches our algorithm for being able to reuse fibers
    // that have completed but haven't yet been pruned (see
    // 'CreateOrReuseFiber()').
    struct TypeErasedFiber {
      void Reuse() {
        done = false;
        // Need to reinitialize the interrupt so that the
        // previous eventual that registered with this
        // interrupt won't get invoked as a handler!
        interrupt.~Interrupt();
        new (&interrupt) class Interrupt();
      }

      virtual ~TypeErasedFiber() = default;

      // A fiber indicates it is done with this boolean.
      bool done = false;

      // Each fiber has it's own interrupt so that we can control how
      // interrupts are propagated.
      class Interrupt interrupt;

      // Each fiber forms a linked list of currently created fibers.
      std::unique_ptr<TypeErasedFiber> next;

      // Need to store a cloned context in which would be stored callback.
      std::optional<Scheduler::Context> context;
    };

    // Returns the fiber created from the templated class 'Adaptor'
    // which actually instantiates a 'Fiber' which has template types.
    //
    // This virtual method enables 'CreateOrReuseFiber()' to be
    // defined in 'TypeErasedAdaptor' so that it doesn't have to be
    // instantiated for every 'Adaptor' (and we pay a small runtime
    // hit of having to make a virtual function call).
    virtual TypeErasedFiber* CreateFiber() = 0;

    // Returns true if all fibers are done.
    //
    // NOTE: expects to be called while holding the lock associated
    // with this instance (i.e., to be called from within
    // 'Synchronized()').
    bool FibersDone() {
      CHECK(lock().OwnedByCurrentSchedulerContext());
      bool done = true;
      TypeErasedFiber* fiber = fibers_.get();
      while (fiber != nullptr) {
        if (!fiber->done) {
          done = false;
          break;
        }
        fiber = fiber->next.get();
      }
      return done;
    }

    // Returns true if a fiber had to be interrupted (i.e., not all
    // fibers are done).
    //
    // NOTE: expects to be called while holding the lock associated
    // with this instance (i.e., to be called from within
    // 'Synchronized()').
    bool InterruptFibers() {
      CHECK(lock().OwnedByCurrentSchedulerContext());
      bool interrupted = false;
      TypeErasedFiber* fiber = fibers_.get();
      while (fiber != nullptr) {
        if (!fiber->done) {
          fiber->interrupt.Trigger();
          interrupted = true;
        }
        fiber = fiber->next.get();
      }
      return interrupted;
    }

    // Returns an eventual which will either create a new fiber or
    // reuse an existing one and return that fiber. The eventual
    // returns nullptr to indicate to downstream eventuals that we've
    // encountered a failure or been interrupted and they should not
    // continue.
    //
    // At somepoint we could use a 'Task' and move this implementation
    // into a .cc file and update the funciton signature to be:
    //
    // Task::To<TypeErasedFiber*> CreateOrReuseFiber();
    //
    // However, there are some deficiencies in 'Task' that need to be
    // addressed before doing so and we should ensure that we see
    // further speed ups that are beyond what we get already from
    // putting all of these in 'TypeErasedAdaptor'.
    template <typename... Errors>
    [[nodiscard]] auto CreateOrReuseFiber(
        stout::borrowed_ref<
            std::optional<
                std::variant<Stopped, Errors...>>>&& stopped_or_error) {
      return Synchronized(Then(
          [this, stopped_or_error = std::move(stopped_or_error)]() {
            // As long as downstream isn't done, or we've been interrupted,
            // or have encountered an error, then add a new fiber if none
            // exist, otherwise trim done fibers from the front, then look
            // for a done fiber to reuse, or if all fibers are not done add
            // a new one.
            TypeErasedFiber* fiber = nullptr;

            if (!(downstream_done_
                  || interrupted_ || stopped_or_error->has_value())) {
              do {
                if (!fibers_) {
                  fibers_.reset(CreateFiber());
                  fiber = fibers_.get();
                } else if (fibers_->done) {
                  // Need to release next before we reset so it
                  // doesn't get deallocated as part of reset.
                  fibers_.reset(fibers_->next.release());
                } else {
                  fiber = fibers_.get();
                  CHECK_NOTNULL(fiber);
                  for (;;) {
                    if (fiber->done) {
                      std::cout << "Reuse fiber" << std::endl;
                      fiber->Reuse();
                      break;
                    } else if (!fiber->next) {
                      std::cout << "Create fiber" << std::endl;
                      fiber->next.reset(CreateFiber());
                      fiber = fiber->next.get();
                      break;
                    } else {
                      fiber = fiber->next.get();
                    }
                  }
                  CHECK_NOTNULL(fiber);
                }
              } while (fiber == nullptr);

              CHECK_NOTNULL(fiber);

              // Mark fibers not done since we're starting one.
              fibers_done_ = false;
            }

            return fiber;
          }));
    }

    // Returns an eventual to handle when the upstream stream has
    // ended. At this point we may still have fibers that are not
    // completed but we know that we won't be getting any more values
    // from upstream.
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    template <typename... Errors>
    [[nodiscard]] auto IngressEpilogue(
        stout::borrowed_ref<
            std::optional<
                std::variant<Stopped, Errors...>>>&& stopped_or_error) {
      return Synchronized(
          Eventual<void>()
              .context(std::move(stopped_or_error))
              .start([this](auto& /* stopped_or_error */, auto& k) {
                std::cout << "Ingress Epilogue" << std::endl;
                upstream_done_ = true;

                fibers_done_ = FibersDone();

                if (fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .fail([this](auto& stopped_or_error, auto& k, auto&& error) {
                upstream_done_ = true;

                if (!stopped_or_error->has_value()) {
                  stopped_or_error->emplace(
                      std::forward<decltype(error)>(error));
                }

                fibers_done_ = FibersDone();

                if (fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .stop([this](auto& stopped_or_error, auto& k) {
                upstream_done_ = true;

                if (!stopped_or_error->has_value()) {
                  stopped_or_error->emplace(eventuals::Stopped());
                }

                fibers_done_ = FibersDone();

                if (fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              }));
    }

    // Returns an eventual which handles when the stream returned from
    // the fiber has ended.
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    template <typename... Errors>
    [[nodiscard]] auto FiberEpilogue(
        TypeErasedFiber* fiber,
        stout::borrowed_ref<
            std::optional<
                std::variant<Stopped, Errors...>>>&& stopped_or_error) {
      return Synchronized(
          Eventual<void>()
              .context(std::move(stopped_or_error))
              .start([this, fiber](auto& /* stopped_or_error */, auto& k) {
                std::cout << "FiberEpilogue start" << std::endl;
                fiber->done = true;

                fibers_done_ = FibersDone();

                if (upstream_done_ && fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .fail([this, fiber](
                        auto& stopped_or_error,
                        auto& k,
                        auto&& error) {
                fiber->done = true;

                if (!stopped_or_error->has_value()) {
                  stopped_or_error->emplace(
                      std::forward<decltype(error)>(error));
                }

                fibers_done_ = !InterruptFibers();

                if (upstream_done_ && fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .stop([this, fiber](auto& stopped_or_error, auto& k) {
                fiber->done = true;

                if (!stopped_or_error->has_value()) {
                  stopped_or_error->emplace(eventuals::Stopped());
                }

                fibers_done_ = !InterruptFibers();

                if (upstream_done_ && fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              }));
    }

    // Returns an eventual which handles when we're interrupted.
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    [[nodiscard]] auto Interrupt() {
      return Synchronized(Then([this]() {
               interrupted_ = true;

               fibers_done_ = !InterruptFibers();

               if (upstream_done_ && fibers_done_) {
                 notify_egress_();
                 notify_done_();
               }
             }))
          >> Terminal();
    }

    // Returns an eventual which will wait for the upstream stream to
    // have ended, the fibers to have finished, and downstream to have
    // requested done (either by us because an exception or stop
    // occured or by downstream themselves).
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    [[nodiscard]] auto WaitForDone(Callback<void()>&& callback) {
      return Synchronized(
                 Wait([this](auto notify) {
                   notify_done_ = std::move(notify);
                   return [this]() {
                     return !downstream_done_
                         || !upstream_done_
                         || !fibers_done_;
                   };
                 }))
          >> Terminal()
                 .start([callback = std::move(callback)]() mutable {
                   // NOTE: we need to move 'callback' on the stack
                   // and invoke it in a 'Terminal' so that in the
                   // event invoking it will cause this eventual that
                   // we're currently executing to get cleaned up we
                   // won't have a possible heap-after-use issue.
                   Callback<void()> callback_on_stack = std::move(callback);
                   callback_on_stack();
                 })
                 .fail([](auto&& unreachable) {
                   static_assert(
                       always_false_v<decltype(unreachable)>,
                       "Unreachable");
                 })
                 .stop([](auto&& unreachable) {
                   static_assert(
                       always_false_v<decltype(unreachable)>,
                       "Unreachable");
                 });
    }

    // Returns an eventual which handles when "downstream" requests
    // the stream to be done (either by us or by the downstream
    // themselves).
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    [[nodiscard]] auto Done() {
      return Synchronized(Then([this]() {
               downstream_done_ = true;

               fibers_done_ = !InterruptFibers();

               if (upstream_done_ && fibers_done_) {
                 notify_done_();
               }

               // TODO(benh): at this point it's possible that
               // upstream is actually waiting because we called
               // 'Next()' on it and it might wait forever! This
               // is a deficiency of eventuals and should be
               // addressed by adding the ability to "interrupt" a
               // stream that we called 'Next()' on, something
               // like 'TypeErasedStream::Interrupt()'.
               //
               // In the meantime applications should handle this
               // situation by interrupting the upstream "out of
               // band" depending on what the upstream is. For
               // example, if upstream is a "queue" or "pipe" or
               // "reader" of some sort then make sure those
               // abstractions have some sort of an 'Interrupt()'
               // like function that you can call _before_ calling
               // 'Done()' on the 'Concurrent()'.
             }))
          >> Terminal();
    }

    // Head of linked list of fibers.
    std::unique_ptr<TypeErasedFiber> fibers_;

    // Callback associated with waiting for "egress", i.e., values
    // from each fiber.
    Callback<void()> notify_egress_;

    bool upstream_done_ = false;
    bool downstream_done_ = false;
    bool fibers_done_ = false;

    // Callback associated with waiting for everything to be done,
    // i.e., upstream done, downstream done, and fibers done.
    Callback<void()> notify_done_;

    // Indicates whether or not we've received an interrupt and we
    // should stop requesting the next upstream value.
    bool interrupted_ = false;
  };

  // 'Adaptor' is our typeful adaptor that the concurrent continuation
  // uses in order to implement the semantics of 'Concurrent()'.
  template <typename F_, typename Arg_, typename UpstreamErrorsAndErrorsFromE_>
  struct Adaptor final : TypeErasedAdaptor {
    Adaptor(
        F_ f,
        stout::borrowed_ref<std::optional<
            variant_of_type_and_tuple_t<
                Stopped,
                UpstreamErrorsAndErrorsFromE_>>>&& stopped_or_error)
      : f_(std::move(f)),
        stopped_or_error_(std::move(stopped_or_error)) {}

    ~Adaptor() override = default;

    // Our typeful fiber includes the continuation 'K' that we'll
    // start for each upstream value.
    template <typename E_>
    struct Fiber : TypeErasedFiber {
      using K = decltype(Build(std::declval<E_>()));
      std::optional<K> k;
    };

    // Returns an eventual which represents the computation we perform
    // for each upstream value.
    [[nodiscard]] auto FiberEventual(TypeErasedFiber* fiber, Arg_&& arg) {
      // NOTE: we do a 'RescheduleAfter()' so that we don't end up
      // borrowing any 'Scheduler::Context' (for example from
      // 'Synchronized()') that might have come from the eventual
      // returned from 'f_()'.
      return RescheduleAfter(
                 // NOTE: 'f_()' should expect to be composed with a
                 // stream hence the use of 'Iterate()'. It also might
                 // return a 'FlatMap()' so we need to use 'Loop()'
                 // down below even though we know we only have a
                 // single 'arg' to iterate from the top.
                 Iterate({std::move(arg)})
                 >> f_())
          >> Synchronized(Map([this](auto&& value) {
               std::cout << "FiberEventual Synch block" << std::endl;
               values_.push_back(std::forward<decltype(value)>(value));
               notify_egress_();
             }))
          >> Loop()
          >> FiberEpilogue(fiber, stopped_or_error_.reborrow())
          >> Terminal();
    }

    // Returns an upcasted 'TypeErasedFiber' from our typeful 'Fiber'.
    TypeErasedFiber* CreateFiber() override {
      using E = decltype(FiberEventual(nullptr, std::declval<Arg_>()));
      return new Fiber<E>();
    }

    // Helper that starts a fiber by downcasting to typeful fiber.
    void StartFiber(TypeErasedFiber* fiber, Arg_&& arg) {
      std::cout << "Start fiber" << std::endl;
      using E = decltype(FiberEventual(fiber, std::move(arg)));

      static_cast<Fiber<E>*>(fiber)->k.emplace(
          Build(FiberEventual(fiber, std::move(arg))));

      // TODO(benh): differentiate the names of the fibers for
      // easier debugging!
      fiber->context.emplace(
          Scheduler::Context::Get()->name() + " [concurrent fiber]");

      fiber->context->scheduler()->Submit(
          [fiber]() {
            CHECK_EQ(&fiber->context.value(), Scheduler::Context::Get().get());
            static_cast<Fiber<E>*>(fiber)->k->Register(fiber->interrupt);
            static_cast<Fiber<E>*>(fiber)->k->Start();
          },
          fiber->context.value());
    }

    // Returns an eventual which implements the logic of handling each
    // upstream value.
    [[nodiscard]] auto Ingress() {
      return Map(Let([this](Arg_& arg) {
               return CreateOrReuseFiber(stopped_or_error_.reborrow())
                   >> Then([&](TypeErasedFiber* fiber) {
                        // A nullptr indicates that we should tell
                        // upstream we're "done" because something
                        // failed or an interrupt was received.
                        std::cout << "Got fiber and run Then" << std::endl;
                        bool done = fiber == nullptr;

                        if (!done) {
                          StartFiber(fiber, std::move(arg));
                        }
                        std::cout << "StartFiber end" << std::endl;

                        return done;
                      });
             }))
          >> Until([](bool done) { return done; })
          >> Loop() // Eagerly try to get next value to run concurrently!
          >> IngressEpilogue(stopped_or_error_.reborrow())
          >> Terminal();
    }

    // Returns an eventual which implements the logic for handling
    // each value emitted from our fibers and moving them downstream.
    [[nodiscard]] auto Egress() {
      return Synchronized(
                 Wait([this](auto notify) {
                   std::cout << "Egress wait" << std::endl;
                   notify_egress_ = std::move(notify);
                   return [this]() {
                     if (values_.empty()) {
                       return !(upstream_done_ && fibers_done_);
                     } else {
                       return false;
                     }
                   };
                 })
                 // Need to check for an exception _before_
                 // 'Until()' because we have no way of hooking
                 // into "ended" after 'Until()'.
                 >> Map([this]() {
                     return Eventual<std::optional<Value_>>()
                         .template raises<UpstreamErrorsAndErrorsFromE_>()
                         .start([this](auto& k) {
                           std::cout << "Egress map" << std::endl;
                           if (stopped_or_error_->has_value()
                               && upstream_done_ && fibers_done_) {
                             // TODO(benh): flush remaining values first?
                             std::visit(
                                 [&k](auto&& stopped_or_error) {
                                   if constexpr (
                                       std::is_same_v<
                                           std::decay_t<
                                               decltype(stopped_or_error)>,
                                           Stopped>) {
                                     k.Stop();
                                   } else {
                                     k.Fail(
                                         std::forward<
                                             decltype(stopped_or_error)>(
                                             stopped_or_error));
                                   }
                                 },
                                 std::move(stopped_or_error_->value()));
                           } else if (!values_.empty()) {
                             auto value = std::move(values_.front());
                             values_.pop_front();
                             k.Start(std::optional<Value_>(std::move(value)));
                           } else {
                             CHECK(upstream_done_ && fibers_done_);
                             k.Start(std::optional<Value_>());
                           }
                         });
                   }))
          >> Until([](std::optional<Value_>& value) {
               return !value;
             })
          >> Map([](std::optional<Value_>&& value) {
               CHECK(value);
               return std::move(*value);
             });
    }

    F_ f_;

    stout::borrowed_ref<
        std::optional<
            variant_of_type_and_tuple_t<
                Stopped,
                UpstreamErrorsAndErrorsFromE_>>>
        stopped_or_error_;

    using Value_ =
        typename decltype(f_())::template ValueFrom<Arg_, std::tuple<>>;

    std::deque<Value_> values_;
  };

  // 'Continuation' is implemented by acting as both a loop for the
  // upstream stream ("ingress") and a stream for downstream
  // ("egress"). We implement the logic for this in 'Adaptor' and use
  // 'Continuation' just to route to the appropriate functionality in
  // 'Adaptor'. We also use 'Continuation' to store the the eventuals
  // returned from 'Adaptor' (vs having to heap allocate them by
  // having them all return 'Task' or 'Generator').
  template <typename K_, typename F_, typename Arg_, typename Errors_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, F_ f)
      : adaptor_(std::move(f), stopped_or_error_.Borrow()),
        k_(std::move(k)) {}

    // NOTE: explicit move-constructor because of 'std::atomic_flag'.
    Continuation(Continuation&& that) noexcept
      : adaptor_(std::move(that.adaptor_.f_), stopped_or_error_.Borrow()),
        interrupt_(std::move(that.interrupt_)),
        handler_(std::move(that.handler_)),
        k_(std::move(that.k_)) {}

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;

      ingress_.emplace(Build<Arg_, Errors_>(adaptor_.Ingress()));

      // NOTE: we don't register an interrupt for 'ingress_' since we
      // explicitly handle interrupts with 'Adaptor::Interrupt()'.
      //
      // NOTE: we wait to start 'ingress_' until 'egress_' invokes 'Next()'.

      wait_for_done_.emplace(Build(
          adaptor_.WaitForDone([this]() { egress_->Ended(); })));

      // NOTE: we don't register an interrupt for 'wait_for_done_'
      // since we explicitly handle interrupts with
      // 'Adaptor::Interrupt()'.

      wait_for_done_->Start();

      // NOTE: we move 'k_' so 'Concurrent()' can't be reused.
      CHECK(!egress_) << "Concurrent() reuse is unsupported";

      egress_.emplace(Build(adaptor_.Egress(), std::move(k_)));

      // NOTE: we don't register an interrupt for 'egress_' since we
      // explicitly handle interrupts with 'Adaptor::Interrupt()'.

      egress_->Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      if (!ingress_) {
        CHECK(!egress_);
        k_.Fail(std::forward<Error>(error));
      } else {
        ingress_->Fail(std::forward<Error>(error));
      }
    }

    void Stop() {
      if (!ingress_) {
        CHECK(!egress_);
        k_.Stop();
      } else {
        ingress_->Stop();
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if (!handler_.has_value()
          || (handler_.has_value()
              && handler_->InstallOrExecuteIfTriggered())
              != Interrupt::Handler::State::EXECUTED) {
        CHECK(ingress_);
        ingress_->Body(std::forward<Args>(args)...);
      }
    }

    void Ended() {
      CHECK(ingress_);
      ingress_->Ended();
    }

    void Next() override {
      // NOTE: we go "down" into egress before going "up" to ingress
      // so that we have saved the 'Wait()' notify callbacks.
      CHECK(egress_);
      egress_->Body();

      // Using std::atomic_flag so we only start ingress once!
      if (!next_.test_and_set()) {
        CHECK(ingress_);
        ingress_->Begin(*CHECK_NOTNULL(stream_));
      }
    }

    void Done() override {
      CHECK(!done_);

      done_.emplace(Build(adaptor_.Done()));

      // NOTE: we don't register an interrupt for 'done_' since we
      // explicitly handle interrupts with 'Adaptor::Interrupt()'.

      done_->Start();
    }

    void Register(Interrupt& interrupt) {
      handler_.emplace(&interrupt, [this]() {
        interrupt_.emplace(Build(adaptor_.Interrupt()));

        // NOTE: we don't register an interrupt for 'done_' since we
        // explicitly handle interrupts with 'Adaptor::Interrupt()'.

        interrupt_->Start();
      });
    }

    using E_ = typename std::invoke_result_t<F_>;

    using UpstreamErrorsAndErrorsFromE_ = tuple_types_union_t<
        typename E_::template ErrorsFrom<Arg_, std::tuple<>>,
        Errors_>;

    // Indicates whether the continuation was stopped or we received
    // a failure and we should stop requesting the next upstream value.
    stout::Borrowable<
        std::optional<
            variant_of_type_and_tuple_t<
                Stopped,
                UpstreamErrorsAndErrorsFromE_>>>
        stopped_or_error_;

    Adaptor<F_, Arg_, UpstreamErrorsAndErrorsFromE_> adaptor_;

    TypeErasedStream* stream_ = nullptr;

    std::atomic_flag next_ = ATOMIC_FLAG_INIT;

    using Ingress_ = decltype(Build<Arg_, Errors_>(adaptor_.Ingress()));
    std::optional<Ingress_> ingress_;

    using Egress_ = decltype(Build(adaptor_.Egress(), std::declval<K_>()));
    std::optional<Egress_> egress_;

    using WaitForDone_ = decltype(Build(
        adaptor_.WaitForDone(std::declval<Callback<void()>>())));
    std::optional<WaitForDone_> wait_for_done_;

    using Done_ = decltype(Build(adaptor_.Done()));
    std::optional<Done_> done_;

    using Interrupt_ = decltype(Build(adaptor_.Interrupt()));
    std::optional<Interrupt_> interrupt_;

    std::optional<Interrupt::Handler> handler_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable final {
    using E_ = typename std::invoke_result_t<F_>;

    template <typename Arg, typename Errors>
    using ValueFrom = typename E_::template ValueFrom<Arg, std::tuple<>>;

    // NOTE: need to union errors because we might propagate errors
    // that we get from upstream if we haven't started streaming ingress
    // values yet.
    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        typename E_::template ErrorsFrom<Arg, std::tuple<>>,
        Errors>;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      static_assert(
          !std::is_void_v<ValueFrom<Arg, Errors>>,
          "'Concurrent' does not (yet) support 'void' eventual values");

      return Continuation<
          K,
          F_,
          Arg,
          Errors>(std::move(k), std::move(f_));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Concurrent(F f) {
  static_assert(
      std::is_invocable_v<F>,
      "Concurrent expects callable that takes no arguments");

  return _Concurrent::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
