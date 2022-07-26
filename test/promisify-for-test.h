#pragma once

#include "eventuals/promisify.h"
#include "test/generate-test-task-name.h"

// Helper that injects a name for 'Promisify()' to simplify call sites.
#define PromisifyForTest(e) \
  ::eventuals::Promisify(GenerateTestTaskName(), (e))
