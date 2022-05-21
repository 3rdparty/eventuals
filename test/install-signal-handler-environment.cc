#include "test/install-signal-handler-environment.h"

testing::Environment* const InstallSignalHandlerEnvironment::environment_ =
    testing::AddGlobalTestEnvironment(new InstallSignalHandlerEnvironment{});
