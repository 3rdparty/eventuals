#include "eventuals/scheduler.h"

#include "glog/logging.h" // For GetTID().
#include "stout/stringify.hpp"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class DefaultScheduler final : public Scheduler {
 public:
  ~DefaultScheduler() override = default;

  bool Continuable(Context*) override {
    return Context::Get()->scheduler() == this;
  }

  void Submit(Callback<void()> callback, Context* context) override {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    Context* previous = Context::Switch(context);

    EVENTUALS_LOG(1)
        << "'" << context->name() << "' preempted '" << previous->name() << "'";

    callback();

    CHECK_EQ(context, Context::Get());

    Context::Switch(previous);
  }

  void Clone(Context* context) override {
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

static thread_local Scheduler::Context context(
    Scheduler::Default(),
    "[thread "
        + stringify(std::this_thread::get_id())
        + " default context]");

////////////////////////////////////////////////////////////////////////

thread_local stout::borrowed_ref<Scheduler::Context>
    Scheduler::Context::current_ = []() {
      return context.Borrow();
    }();

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
