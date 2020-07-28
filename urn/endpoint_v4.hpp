#pragma once

/**
 * \file urn/endpoint_v4.hpp
 * AF_INET sockaddr wrapper
 */

#include <urn/__bits/lib.hpp>
#include <urn/error.hpp>

#if __urn_os_linux || __urn_os_macos
  #include <arpa/inet.h>
  #include <netinet/in.h>
#elif __urn_os_windows
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32")
#else
  #error Unsupported platform
#endif


__urn_begin


struct endpoint_v4: sockaddr_storage
{
  endpoint_v4 (uint32_t a, uint16_t p) noexcept
  {
    native().sin_family = AF_INET;
    address(a);
    port(p);
  }


  const sockaddr_in &native () const noexcept
  {
    return *reinterpret_cast<const sockaddr_in *>(this);
  }


  sockaddr_in &native () noexcept
  {
    return *reinterpret_cast<sockaddr_in *>(this);
  }


  uint32_t address () const noexcept
  {
    return ntohl(native().sin_addr.s_addr);
  }


  void address (uint32_t v) noexcept
  {
    native().sin_addr.s_addr = htonl(v);
  }


  uint16_t port () const noexcept
  {
    return ntohs(native().sin_port);
  }


  void port (uint16_t v) noexcept
  {
    native().sin_port = htons(v);
  }


  size_t hash () const noexcept;

  friend inline bool operator== (const endpoint_v4 &l, const endpoint_v4 &r)
    noexcept
  {
    auto ln = l.native(), rn = r.native();
    return ln.sin_addr.s_addr == rn.sin_addr.s_addr && ln.sin_port == rn.sin_port;
  }
};


struct already_checked_tag {};
constexpr already_checked_tag already_checked{};


inline const endpoint_v4 *as_endpoint_v4 (const sockaddr_storage &storage,
  already_checked_tag) noexcept
{
    return static_cast<const endpoint_v4 *>(&storage);
}


inline const endpoint_v4 *as_endpoint_v4 (const sockaddr_storage &storage,
  std::error_code &error) noexcept
{
  if (storage.ss_family == AF_INET)
  {
    error.clear();
    return as_endpoint_v4(storage, already_checked);
  }
  error = make_error_code(errc::not_inet_v4_endpoint);
  return nullptr;
}


__urn_end


namespace std
{

template <>
struct hash<urn::endpoint_v4>
{
  size_t operator() (const urn::endpoint_v4 &a) const noexcept
  {
    return a.hash();
  }
};

} // namespace std
