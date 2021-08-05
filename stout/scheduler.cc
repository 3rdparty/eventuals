#include "stout/scheduler.h"

namespace stout {
namespace eventuals {

Scheduler* Scheduler::default_ = new Scheduler();

thread_local Scheduler* Scheduler::scheduler_ = default_;

thread_local Scheduler::Context* Scheduler::context_ =
    new Scheduler::Context(new std::string("[default]"));

} // namespace eventuals
} // namespace stout
