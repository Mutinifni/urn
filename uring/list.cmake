find_library(uring_LIBRARY NAMES uring)
find_path(uring_INCLUDE_DIR NAMES liburing.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(uring
  FOUND_VAR uring_FOUND
  REQUIRED_VARS uring_LIBRARY uring_INCLUDE_DIR
)

find_package(Threads REQUIRED)

list(APPEND urn_experiments uring)

list(APPEND urn_uring_sources
  uring/relay.cpp
  uring/main.cpp
)

list(APPEND urn_uring_libs ${uring_LIBRARY} Threads::Threads)
