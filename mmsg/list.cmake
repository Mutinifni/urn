list(APPEND urn_experiments mmsg)

find_package(Threads REQUIRED)

list(APPEND urn_mmsg_sources
  mmsg/main.cpp
  mmsg/relay.hpp
  mmsg/relay.cpp
)

list(APPEND urn_mmsg_libs Threads::Threads)
