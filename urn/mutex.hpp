#pragma once

/**
 * \file urn/mutex.hpp
 * Mutex wrapper or dummy impl depending on MaxThreads
 */

#include <urn/__bits/lib.hpp>
#include <shared_mutex>


__urn_begin


template <bool MultiThreaded>
struct shared_mutex: public std::shared_mutex
{
  using std::shared_mutex::shared_mutex;
};


template <>
struct shared_mutex<false>
{
  shared_mutex () = default;

  shared_mutex (const shared_mutex &) = delete;
  shared_mutex (shared_mutex &&) = delete;

  shared_mutex &operator= (const shared_mutex &) = delete;
  shared_mutex &operator= (shared_mutex &&) = delete;

  void lock () noexcept
  { }

  void unlock () noexcept
  { }

  void lock_shared () noexcept
  { }

  void unlock_shared () noexcept
  { }
};


__urn_end
