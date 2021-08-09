#include "stout/scheduler.h"

#include "glog/logging.h" // For GetTID().

////////////////////////////////////////////////////////////////////////

// Forward declaration.
//
// TODO(benh): don't use private functions from glog; currently we're
// doing this to match glog message thread id's but this is prone to
// breaking if glog makes any internal changes.
namespace google {
namespace glog_internal_namespace_ {

pid_t GetTID();

} // namespace glog_internal_namespace_
} // namespace google

////////////////////////////////////////////////////////////////////////

static auto GetTID() {
  return static_cast<unsigned int>(google::glog_internal_namespace_::GetTID());
}

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

class DefaultScheduler : public Scheduler {
 public:
  struct DefaultContext : public Context {
    DefaultContext(Scheduler* scheduler, std::string name)
      : Context(scheduler),
        name_(std::move(name)) {}

    const std::string& name() override {
      return name_;
    }

    std::string name_;
  };

  bool Continue(Context*) override {
    return Context::Get()->scheduler() == this;
  }

  void Submit(Callback<> callback, Context* context) override {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    Context* previous = Context::Switch(context);

    STOUT_EVENTUALS_LOG(1)
        << "'" << context->name() << "' preempted '" << previous->name() << "'";

    callback();

    CHECK_EQ(context, Context::Get());

    Context::Switch(previous);
  }
};

////////////////////////////////////////////////////////////////////////

Scheduler* Scheduler::Default() {
  static Scheduler* scheduler = new DefaultScheduler();
  return scheduler;
}

////////////////////////////////////////////////////////////////////////

static thread_local DefaultScheduler::DefaultContext context(
    Scheduler::Default(),
    "[" + std::to_string(static_cast<unsigned int>(GetTID())) + "]");

////////////////////////////////////////////////////////////////////////

thread_local Scheduler::Context* Scheduler::Context::current_ = &context;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
