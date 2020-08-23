cmake_minimum_required(VERSION 3.15)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(urn LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")


# User settings {{{1

# Experiments
# (note: all are turned off for Coverage build)
option(urn_libuv "Experiment with libuv" ON)

# Business logic settings
option(urn_unittests "Build unittests" ON)
option(urn_benchmarks "Build benchmarking application" OFF)

if(CMAKE_BUILD_TYPE MATCHES Coverage)
  set(CMAKE_BUILD_TYPE "Debug")
  set(Coverage ON)
  set(urn_unittests ON)
  set(urn_benchmarks OFF)
  set(urn_libuv OFF)
endif()


# Build settings {{{1

# compiler
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  include(cmake/g++.cmake)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  include(cmake/clang++.cmake)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  include(cmake/msvc.cmake)
else()
  message(WARNING "Unknown compiler")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# host
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  include(cmake/macos.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  include(cmake/linux.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  include(cmake/windows.cmake)
else()
  message(WARNING "Unknown system")
endif()


# Targets {{{1

include(urn/list.cmake)

add_library(urn INTERFACE)
add_library(urn::urn ALIAS urn)
#target_sources(urn INTERFACE ${urn_sources})
target_include_directories(urn
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)


# Tests {{{2
if(urn_unittests)
  message(STATUS "Enable unittests")
  enable_testing()
  include(cmake/catchorg_catch2.cmake)
  add_executable(unittests ${urn_unittests_sources})
  target_compile_options(unittests PRIVATE ${max_warning_flags})
  target_link_libraries(unittests urn::urn Catch2::Catch2)
  include(extern/catchorg_catch2/contrib/Catch.cmake)
  catch_discover_tests(unittests)
endif()


# Benchmarks {{{2
if(urn_benchmarks)
  message(STATUS "Enable benchmarks")
  include(cmake/google_benchmark.cmake)
  include(bench/list.cmake)
  add_executable(benchmarks ${urn_benchmarks_sources})
  target_link_libraries(benchmarks urn::urn benchmark)
endif()


# Experiments {{{2
if(urn_libuv)
  include(libuv/list.cmake)
endif()

foreach(experiment ${urn_experiments})
  # target per experiment
  message(STATUS "Enable ${experiment} experiment")
  add_executable(urn_${experiment} ${urn_${experiment}_sources})
  target_compile_options(urn_${experiment} PRIVATE ${max_warning_flags})
  target_include_directories(urn_${experiment} PRIVATE ${${experiment}_INCLUDE_DIR})
  if(urn_${experiment}_libs)
    target_link_libraries(urn_${experiment} urn::urn ${urn_${experiment}_libs})
  endif()
endforeach()
