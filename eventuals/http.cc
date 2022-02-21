#include "eventuals/http.h"

////////////////////////////////////////////////////////////////////////

class CURLGlobalInitializer final {
 public:
  CURLGlobalInitializer() {
    CHECK_EQ(curl_global_init(CURL_GLOBAL_ALL), 0);
  }

  ~CURLGlobalInitializer() {
    curl_global_cleanup();
  }
};

static CURLGlobalInitializer initializer_;

////////////////////////////////////////////////////////////////////////
