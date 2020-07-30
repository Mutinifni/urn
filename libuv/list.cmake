find_library(libuv_LIBRARY NAMES uv)
find_path(libuv_INCLUDE_DIR NAMES uv.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libuv
  FOUND_VAR libuv_FOUND
  REQUIRED_VARS libuv_LIBRARY libuv_INCLUDE_DIR
)

if(NOT libuv_FOUND)
  if(NOT EXISTS extern/libuv_libuv/CMakeLists.txt)
    find_package(Git)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init extern/libuv_libuv
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_subdirectory(extern/libuv_libuv)

    set(libuv_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/libuv_libuv/include)
    set(libuv_LIBRARY uv_a)
  endif()
endif()

list(APPEND urn_experiments libuv)

list(APPEND urn_libuv_sources
  libuv/main.cpp
)

list(APPEND urn_libuv_libs ${libuv_LIBRARY})
