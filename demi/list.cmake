list(APPEND urn_experiments demi)

list(APPEND urn_demi_sources
  demi/main.cpp
  demi/relay.hpp
  demi/relay.cpp
)

set(demi_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/demi/)

# demi
#list(APPEND urn_mmsg_libs Threads::Threads)
