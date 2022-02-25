#pragma once

// Helper that expects a std::exception (or something derived from it)
// to be thrown with a 'what()' equal to '_what_'.
#define EXPECT_THROW_WHAT(_expression_, _what_)                     \
  try {                                                             \
    (_expression_);                                                 \
    ADD_FAILURE_AT(__FILE__, __LINE__) << "no exception thrown";    \
  } catch (const std::exception& e) {                               \
    if (std::string(e.what()) != std::string(_what_)) {             \
      ADD_FAILURE_AT(__FILE__, __LINE__)                            \
          << "std::exception::what() is '" << e.what()              \
          << "' which does not match '" << (_what_) << "'";         \
    }                                                               \
  } catch (...) {                                                   \
    ADD_FAILURE_AT(__FILE__, __LINE__)                              \
        << "caught exception does not inherit from std::exception"; \
  }
