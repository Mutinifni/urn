#pragma once

/**
 * \file urn/mutex.hpp
 * Mutex wrapper or dummy impl depending on MaxThreads
 */

#include <urn/__bits/lib.hpp>
#include <mutex>


__urn_begin


template <size_t MaxThreads>
struct mutex: public std::mutex
{
  static_assert(MaxThreads > 1);
  using std::mutex::mutex;
};


template <>
struct mutex<1>
{
  mutex () = default;

  mutex (const mutex &) = delete;
  mutex (mutex &&) = delete;

  mutex &operator= (const mutex &) = delete;
  mutex &operator= (mutex &&) = delete;

  void lock () noexcept
  { }

  void unlock () noexcept
  { }
};


__urn_end
