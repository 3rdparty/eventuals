#include "stout/scheduler.h"

namespace stout {
namespace eventuals {

Scheduler* Scheduler::default_ = new Scheduler();
thread_local Scheduler* Scheduler::scheduler_ = default_;
thread_local void* Scheduler::context_ = nullptr;

} // namespace eventuals {
} // namespace stout {
