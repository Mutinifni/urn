find_package(Seastar REQUIRED)

list(APPEND urn_experiments seastar)

list(APPEND urn_seastar_sources
  seastar/main.cpp
)

list(APPEND urn_seastar_libs Seastar::seastar)
