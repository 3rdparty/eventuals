#pragma once

#include <string>

#include "eventuals/scheduler.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class DynamicThreadPool final : public Scheduler {
 public:
  static DynamicThreadPool& Scheduler() {
    static DynamicThreadPool pool;
    return pool;
  }

  DynamicThreadPool() {}

  ~DynamicThreadPool() override {}

  bool Continuable(const Context& context) override {
    LOG(FATAL) << "unimplemented";
    return false;
  }

  void Submit(Callback<void()> callback, Context& context) override {
    LOG(FATAL) << "unimplemented";
  }

  void Clone(Context& child) override {
    LOG(FATAL) << "unimplemented";
  }

  // TODO(benh): take 'Requirements' just like 'StaticThreadPool'.
  template <typename E>
  [[nodiscard]] auto Schedule(std::string&& name, E e) {
    // TODO(benh): implement me!
    return std::move(e);
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
