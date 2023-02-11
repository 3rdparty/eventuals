#include "eventuals/scheduler.h"

#include "glog/logging.h" // For GetTID().
#include "stout/stringify.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class DefaultScheduler final : public Scheduler {
 public:
  ~DefaultScheduler() override = default;

  bool Continuable(const Context&) override {
    return Context::Get()->scheduler() == this;
  }

  void Submit(Callback<void()> callback, Context& context) override {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    CHECK_EQ(this, context.scheduler());

    CHECK(!context.blocked()) << context.name();

    CHECK_NE(&context, &Context::Default())
        << "Default context should not be used when submitting!";

    stout::borrowed_ref<Context> previous =
        Reborrow(Context::Switch(Borrow(context)));

    EVENTUALS_LOG(1)
        << "'" << context.name() << "' preempted '" << previous->name() << "'";

    callback();

    //////////////////////////////////////////////////////////
    // NOTE: can't use 'context' at this point              //
    // in time because it might have been deallocated!      //
    //////////////////////////////////////////////////////////

    Context::Switch(std::move(previous));

    // TODO(benh): check that the returned context pointer is the same
    // as what we switched to (but nothing more because it might have
    // been deallocated) or is the default context because the context
    // blocked (in which case we can check if it's blocked because
    // we're the only ones that would unblock and run it!)
  }

  void Clone(Context& context) override {
    // This function is intentionally empty because the 'DefaultScheduler'
    // just invokes what ever callback was specified to 'Submit()'.
  }
};

////////////////////////////////////////////////////////////////////////

Scheduler* Scheduler::Default() {
  static Scheduler* scheduler = new DefaultScheduler();
  return scheduler;
}

////////////////////////////////////////////////////////////////////////

thread_local Scheduler::Context Scheduler::Context::default_(
    Scheduler::Default(),
    "[thread "
        + stringify(std::this_thread::get_id())
        + " default context]");

////////////////////////////////////////////////////////////////////////

thread_local stout::borrowed_ref<Scheduler::Context>
    Scheduler::Context::current_ = Borrow(default_);

////////////////////////////////////////////////////////////////////////

Scheduler::Context& Scheduler::Context::Default() {
  return default_;
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
