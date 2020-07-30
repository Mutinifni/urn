#include <iostream>
#include <uv.h>


int main ()
{
  uv_loop_t loop;
  uv_loop_init(&loop);

  uv_run(&loop, UV_RUN_DEFAULT);

  uv_loop_close(&loop);
  return EXIT_SUCCESS;
}
