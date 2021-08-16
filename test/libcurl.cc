#define CURL_STATICLIB
#include <iostream>

#include "curl/curl.h"
#include "gtest/gtest.h"

TEST(Libcurl, Test) {
  // This function must be the first function to call, and it returns a CURL
  // easy handle that you must use as input to other functions in the easy
  // interface This call MUST have a corresponding call to curl_easy_cleanup
  // when the operation is complete.
  CURL* curl_handle = curl_easy_init();
  ASSERT_TRUE(curl_handle);
  if (curl_handle) {
    // set url address
    CURLcode res =
        curl_easy_setopt(curl_handle, CURLOPT_URL, "http://www.google.com");
    EXPECT_EQ(CURLE_OK, res);
    std::cout << "================================================"
              << std::endl;
    // executing a request
    res = curl_easy_perform(curl_handle);
    std::cout << std::endl
              << "================================================"
              << std::endl;
    EXPECT_EQ(CURLE_OK, res);
    // closing the handle curl
    curl_easy_cleanup(curl_handle);
  }
}