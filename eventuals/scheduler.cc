#include "eventuals/scheduler.h"

#include "eventuals/concurrent.h"
#include "glog/logging.h" // For GetTID().

////////////////////////////////////////////////////////////////////////

// Forward declaration.
//
// TODO(benh): don't use private functions from glog; currently we're
// doing this to match glog message thread id's but this is prone to
// breaking if glog makes any internal changes.
#ifdef _WIN32
using pid_t = int;
#endif

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

  DefaultScheduler()
    : Scheduler(Scheduler::SchedulerType::DefaultScheduler_) {}

  bool Continuable(Context*) override {
    return Context::Get()->scheduler() == this;
  }

  void Submit(Callback<> callback, Context* context) override {
    // Default scheduler does not defer because it can't (unless we
    // update all calls that "wait" on tasks to execute outstanding
    // callbacks).
    if (context->data_) {
      context = context->data_;
    }

    Context* previous = Context::Switch(context);

    EVENTUALS_LOG(1)
        << "'" << context->name() << "' preempted '" << previous->name() << "'";

    callback();

    CHECK_EQ(context, Context::Get());

    Context::Switch(previous);
  }

  void Clone(Context* context) override {
    context->data_ =
        new DefaultContext(context->scheduler(), "default context");
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

////////////////////////////////////////////////////////////////////////
