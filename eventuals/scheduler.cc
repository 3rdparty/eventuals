#include "eventuals/scheduler.h"

#include "eventuals/compose.h"
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

    stout::borrowed_ref<Context> previous = Context::Switch(context.Borrow());

    EVENTUALS_LOG(1)
        << "'" << context.name() << "' preempted '" << previous->name() << "'";

    bool context_running = running(context);
    set_running(context, true);

    callback();

    CHECK_EQ(&context, Context::Switch(std::move(previous)).get());

    set_running(context, context_running);
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
    Scheduler::Context::current_ = []() {
      return default_.Borrow();
    }();

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
