find_library(demi_LIBRARY NAMES catnip_libos HINTS "/scratch/pratyush/demikernel/build/src/rust/catnip_libos/lib/")
#find_library(dpdk_LIBRARY NAMES dpdk HINTS "/scratch/pratyush/demikernel/build/ExternalProject/dpdk/lib/")
ADD_LIBRARY(dpdk_LIBRARY STATIC IMPORTED)
SET_TARGET_PROPERTIES(dpdk_LIBRARY PROPERTIES IMPORTED_LOCATION "/scratch/pratyush/demikernel/build/ExternalProject/dpdk/lib/")
LINK_DIRECTORIES("/scratch/pratyush/demikernel/build/ExternalProject/dpdk/lib/")


list(APPEND urn_experiments demi)

list(APPEND urn_demi_sources
  demi/main.cpp
  demi/relay.hpp
  demi/relay.cpp
)

set(demi_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/demi/)

# demi
#list(APPEND urn_mmsg_libs Threads::Threads)
list(APPEND urn_demi_libs ${demi_LIBRARY})
list(APPEND urn_demi_libs ${dpdk_LIBRARY})
#list(APPEND urn_demi_libs "/scratch/pratyush/demikernel/build/ExternalProject/dpdk/lib/libdpdk.a")
