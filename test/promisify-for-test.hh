#pragma once

#include "eventuals/promisify.hh"
#include "test/generate-test-task-name.hh"

// Helper that injects a name for 'Promisify()' to simplify call sites.
#define PromisifyForTest(e) \
  ::eventuals::Promisify(GenerateTestTaskName(), (e))
