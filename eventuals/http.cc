#include "eventuals/http.h"

////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////

class CURLGlobalInitializer final {
 public:
  CURLGlobalInitializer() {
    CHECK_EQ(curl_global_init(CURL_GLOBAL_ALL), 0);
  }

  CURLGlobalInitializer(const CURLGlobalInitializer&) = default;
  CURLGlobalInitializer(CURLGlobalInitializer&&) noexcept = default;

  CURLGlobalInitializer& operator=(const CURLGlobalInitializer&) = default;
  CURLGlobalInitializer& operator=(CURLGlobalInitializer&&) noexcept = default;

  ~CURLGlobalInitializer() {
    curl_global_cleanup();
  }
};

const CURLGlobalInitializer initializer_;

////////////////////////////////////////////////////////////////////////

} // namespace

////////////////////////////////////////////////////////////////////////
