list(APPEND urn_sources
  urn/__bits/lib.hpp
  urn/__bits/platform_sdk.hpp
  urn/endpoint_v4.hpp
  urn/endpoint_v4.cpp
  urn/error.hpp
  urn/error.cpp
)

list(APPEND urn_unittests_sources
  urn/common.test.hpp
  urn/common.test.cpp
  urn/endpoint_v4.test.cpp
  urn/error.test.cpp
)
