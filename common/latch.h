#pragma once

#include <condition_variable>
#include <thread>

namespace urn {

struct countdown_latch {
  countdown_latch(int64_t count) : count{count} {}

  void wait() {
    std::unique_lock<std::mutex> lock(mtx);
    int64_t gen = generation;

    if (--count == 0) {
      ++generation;
      cond.notify_all();
      return;
    }

    cond.wait(lock, [&]() { return gen != generation; });
  }

  int64_t count;
  int64_t generation = 0;
  std::mutex mtx = {};
  std::condition_variable cond = {};
};
} // namespace urn
