#include "stout/scheduler.h"

namespace stout {
namespace eventuals {

Scheduler Scheduler::default_;
thread_local Scheduler* Scheduler::scheduler_ = &default_;
thread_local void* Scheduler::context_ = nullptr;

thread_local bool StaticThreadPool::member = false;
thread_local unsigned int StaticThreadPool::core = 0;

} // namespace eventuals {
} // namespace stout {
