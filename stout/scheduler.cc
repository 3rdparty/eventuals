#include "stout/scheduler.h"

namespace stout {
namespace eventuals {

Scheduler* Scheduler::default_ = new Scheduler();

thread_local Scheduler::Context* Scheduler::Context::current_ =
    new Scheduler::Context(default_, new std::string("[main]"));

} // namespace eventuals
} // namespace stout
