#include "gtest/gtest.h"

#include "uv.h"

TEST(Libuv, Test)
{
  uv_loop_t loop;
  uv_loop_init(&loop);
  uv_run(&loop, UV_RUN_DEFAULT);
  uv_loop_close(&loop);
}
