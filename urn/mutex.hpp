#pragma once

/**
 * \file urn/mutex.hpp
 * Mutex wrapper or dummy impl depending on MaxThreads
 */

#include <urn/__bits/lib.hpp>
#include <mutex>


__urn_begin


template <bool MultiThreaded>
struct mutex: public std::mutex
{
  using std::mutex::mutex;
};


template <>
struct mutex<false>
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
