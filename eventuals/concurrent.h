#pragma once

#include <deque>
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
auto Concurrent(F f);

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
    auto CreateOrReuseFiber() {
      return Synchronized(Then([this]() {
        // As long as downstream isn't done, or we've been interrupted,
        // or have encountered an error, then add a new fiber if none
        // exist, otherwise trim done fibers from the front, then look
        // for a done fiber to reuse, or if all fibers are not done add
        // a new one.
        TypeErasedFiber* fiber = nullptr;

        if (!(downstream_done_ || interrupted_ || exception_)) {
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
                  fiber->Reuse();
                  break;
                } else if (!fiber->next) {
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
    auto IngressEpilogue() {
      return Synchronized(
          Eventual<void>()
              .start([this](auto& k) {
                upstream_done_ = true;

                fibers_done_ = FibersDone();

                if (fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .fail([this](auto& k, auto&& error) {
                upstream_done_ = true;

                if (!exception_) {
                  exception_ = make_exception_ptr_or_forward(
                      std::forward<decltype(error)>(error));
                }

                fibers_done_ = FibersDone();

                if (fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .stop([this](auto& k) {
                upstream_done_ = true;

                if (!exception_) {
                  exception_ = std::make_exception_ptr(
                      eventuals::StoppedException());
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
    auto FiberEpilogue(TypeErasedFiber* fiber) {
      return Synchronized(
          Eventual<void>()
              .start([this, fiber](auto& k) {
                fiber->done = true;

                fibers_done_ = FibersDone();

                if (upstream_done_ && fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .fail([this, fiber](auto& k, auto&& error) {
                fiber->done = true;

                if (!exception_) {
                  exception_ = make_exception_ptr_or_forward(
                      std::forward<decltype(error)>(error));
                }

                fibers_done_ = !InterruptFibers();

                if (upstream_done_ && fibers_done_) {
                  notify_egress_();
                  notify_done_();
                }

                k.Start(); // Exits the synchronized block!
              })
              .stop([this, fiber](auto& k) {
                fiber->done = true;

                if (!exception_) {
                  exception_ = std::make_exception_ptr(
                      eventuals::StoppedException());
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
    auto Interrupt() {
      return Synchronized(Then([this]() {
               interrupted_ = true;

               fibers_done_ = !InterruptFibers();

               if (upstream_done_ && fibers_done_) {
                 notify_egress_();
                 notify_done_();
               }
             }))
          | Terminal();
    }

    // Returns an eventual which will wait for the upstream stream to
    // have ended, the fibers to have finished, and downstream to have
    // requested done (either by us because an exception or stop
    // occured or by downstream themselves).
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    auto WaitForDone(Callback<>&& callback) {
      return Synchronized(
                 Wait([this](auto notify) {
                   notify_done_ = std::move(notify);
                   return [this]() {
                     return !downstream_done_
                         || !upstream_done_
                         || !fibers_done_;
                   };
                 }))
          | Then([callback = std::move(callback)]() mutable {
               callback();
             })
          | Terminal();
    }

    // Returns an eventual which handles when "downstream" requests
    // the stream to be done (either by us or by the downstream
    // themselves).
    //
    // NOTE: this could also be moved to a .cc file but see the
    // comments above 'CreateOrReuseFiber()' above for what's
    // preventing us from doing so.
    auto Done() {
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
          | Terminal();
    }

    // Head of linked list of fibers.
    std::unique_ptr<TypeErasedFiber> fibers_;

    // Callback associated with waiting for "egress", i.e., values
    // from each fiber.
    Callback<> notify_egress_;

    bool upstream_done_ = false;
    bool downstream_done_ = false;
    bool fibers_done_ = false;

    // Callback associated with waiting for everything to be done,
    // i.e., upstream done, downstream done, and fibers done.
    Callback<> notify_done_;

    // Indicates whether or not we've received an interrupt and we
    // should stop requesting the next upstream value.
    bool interrupted_ = false;

    // Indicates whether or not we've got a failure and we should stop
    // requesting the next upstream value.
    std::optional<std::exception_ptr> exception_;
  };

  // 'Adaptor' is our typeful adaptor that the concurrent continuation
  // uses in order to implement the semantics of 'Concurrent()'.
  template <typename F_, typename Arg_>
  struct Adaptor final : TypeErasedAdaptor {
    Adaptor(F_ f)
      : f_(std::move(f)) {}

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
    auto FiberEventual(TypeErasedFiber* fiber, Arg_&& arg) {
      // NOTE: 'f_()' should expect to be composed with a
      // stream hence the use of 'Iterate()'. It also might
      // return a 'FlatMap()' so we need to use 'Loop()'
      // down below even though we know we only have a single
      // 'arg' to iterate from the top.
      return Iterate({std::move(arg)})
          | f_()
          | Synchronized(Map([this](auto&& value) {
               values_.push_back(std::forward<decltype(value)>(value));
               notify_egress_();
             }))
          | Loop()
          | FiberEpilogue(fiber)
          | Terminal();
    }

    // Returns an upcasted 'TypeErasedFiber' from our typeful 'Fiber'.
    TypeErasedFiber* CreateFiber() override {
      using E = decltype(FiberEventual(nullptr, std::declval<Arg_>()));
      return new Fiber<E>();
    }

    // Helper that starts a fiber by downcasting to typeful fiber.
    void StartFiber(TypeErasedFiber* fiber, Arg_&& arg) {
      using E = decltype(FiberEventual(fiber, std::move(arg)));

      static_cast<Fiber<E>*>(fiber)->k.emplace(
          Build(FiberEventual(fiber, std::move(arg))));

      // TODO(benh): differentiate the names of the fibers for
      // easier debugging!
      fiber->context.emplace(
          Scheduler::Context::Get()->name() + " [concurrent fiber]");

      fiber->context->scheduler()->Submit(
          [fiber]() {
            CHECK_EQ(&fiber->context.value(), Scheduler::Context::Get());
            static_cast<Fiber<E>*>(fiber)->k->Register(fiber->interrupt);
            static_cast<Fiber<E>*>(fiber)->k->Start();
          },
          &fiber->context.value());
    }

    // Returns an eventual which implements the logic of handling each
    // upstream value.
    auto Ingress() {
      return Map(Let([this](Arg_& arg) {
               return CreateOrReuseFiber()
                   | Then([&](TypeErasedFiber* fiber) {
                        // A nullptr indicates that we should tell
                        // upstream we're "done" because something
                        // failed or an interrupt was received.
                        bool done = fiber == nullptr;

                        if (!done) {
                          StartFiber(fiber, std::move(arg));
                        }

                        return done;
                      });
             }))
          | Until([](bool done) { return done; })
          | Loop() // Eagerly try to get next value to run concurrently!
          | IngressEpilogue()
          | Terminal();
    }

    // Returns an eventual which implements the logic for handling
    // each value emitted from our fibers and moving them downstream.
    auto Egress() {
      return Synchronized(
                 Wait([this](auto notify) {
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
                 | Map([this]() {
                     return Eventual<std::optional<Value_>>()
                         .start([this](auto& k) {
                           if (exception_ && upstream_done_ && fibers_done_) {
                             // TODO(benh): flush remaining values first?
                             try {
                               std::rethrow_exception(*exception_);
                             } catch (const StoppedException&) {
                               k.Stop();
                             } catch (...) {
                               k.Fail(std::current_exception());
                             }
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
          | Until([](std::optional<Value_>& value) {
               return !value;
             })
          | Map([](std::optional<Value_>&& value) {
               CHECK(value);
               return std::move(*value);
             });
    }

    F_ f_;

    using Value_ = typename decltype(f_())::template ValueFrom<Arg_>;
    std::deque<Value_> values_;
  };

  // 'Continuation' is implemented by acting as both a loop for the
  // upstream stream ("ingress") and a stream for downstream
  // ("egress"). We implement the logic for this in 'Adaptor' and use
  // 'Continuation' just to route to the appropriate functionality in
  // 'Adaptor'. We also use 'Continuation' to store the the eventuals
  // returned from 'Adaptor' (vs having to heap allocate them by
  // having them all return 'Task' or 'Generator').
  template <typename K_, typename F_, typename Arg_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k, F_ f)
      : adaptor_(std::move(f)),
        k_(std::move(k)) {}

    // NOTE: explicit move-constructor because of 'std::atomic_flag'.
    Continuation(Continuation&& that)
      : adaptor_(std::move(that.adaptor_.f_)),
        k_(std::move(that.k_)) {}

    ~Continuation() override = default;

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;

      ingress_.emplace(Build<Arg_>(adaptor_.Ingress()));

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
      CHECK(ingress_);
      ingress_->Body(std::forward<Args>(args)...);
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

      handler_->Install();
    }

    Adaptor<F_, Arg_> adaptor_;

    TypeErasedStream* stream_ = nullptr;

    std::atomic_flag next_ = ATOMIC_FLAG_INIT;

    using Ingress_ = decltype(Build<Arg_>(adaptor_.Ingress()));
    std::optional<Ingress_> ingress_;

    using Egress_ = decltype(Build(adaptor_.Egress(), std::declval<K_>()));
    std::optional<Egress_> egress_;

    using WaitForDone_ = decltype(Build(
        adaptor_.WaitForDone(std::declval<Callback<>>())));
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
    template <typename Arg>
    using ValueFrom =
        typename std::invoke_result_t<F_>::template ValueFrom<Arg>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>(std::move(k), std::move(f_));
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Concurrent(F f) {
  return _Concurrent::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
