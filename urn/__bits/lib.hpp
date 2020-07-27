#pragma once

#include <cstddef>
#include <cstdint>


// project namespace
#define __urn_begin namespace urn { inline namespace v0 {
#define __urn_end   }} // namespace urn::v0


__urn_begin


//
// build OS
//

#define __urn_os_linux 0
#define __urn_os_macos 0
#define __urn_os_windows 0

#if defined(__linux__)
  #undef __urn_os_linux
  #define __urn_os_linux 1
#elif defined(__APPLE__)
  #undef __urn_os_macos
  #define __urn_os_macos 1
#elif defined(_WIN32) || defined(_WIN64)
  #undef __urn_os_windows
  #define __urn_os_windows 1
#endif

constexpr bool is_linux_build = __urn_os_linux == 1;
constexpr bool is_macos_build = __urn_os_macos == 1;
constexpr bool is_windows_build = __urn_os_windows == 1;


//
// build configuration
//

constexpr bool is_release_build =
#if NDEBUG
  true
#else
  false
#endif
;


__urn_end
