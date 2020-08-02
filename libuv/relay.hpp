#pragma once

/**
 * \file libuv/relay.hpp
 */

#include <urn/relay.hpp>
#include <uv.h>
#include <cstdlib>
#include <iostream>


// TODO: http://docs.libuv.org/en/v1.x/udp.html#c.uv_udp_using_recvmmsg


namespace urn_libuv {


inline void die_on_error (int code, const char *fn)
{
  if (code < 0)
  {
    std::cout
      << fn
      << ": "
      << uv_strerror(code)
      << " ("
      << uv_err_name(code)
      << ")\n";
    abort();
  }
}

#define libuv_call(F, ...) die_on_error(F(__VA_ARGS__), #F)


struct config //{{{1
{
  struct
  {
    uint16_t port = 3478;
    size_t reads = 10;
  } client;

  struct
  {
    uint16_t port = 3479;
    size_t reads = 10;
  } peer;
};


struct libuv //{{{1
{
  using endpoint = sockaddr;
  using time = decltype(uv_hrtime());

  struct packet;
  struct client;
  struct peer;
  struct session;
};


struct libuv::packet //{{{1
  : uv_buf_t
{
  packet (const uv_buf_t &buf, size_t len) noexcept
    : uv_buf_t(uv_buf_init(buf.base, (int)len))
  { }

  const std::byte *data () const noexcept
  {
    return reinterpret_cast<const std::byte *>(uv_buf_t::base);
  }

  size_t size () const noexcept
  {
    return uv_buf_t::len;
  }
};


struct libuv::client //{{{1
{
  uv_udp_t socket{};

  client (const config &conf) noexcept;

  void start_receive () noexcept
  { }
};


struct libuv::peer //{{{1
{
  uv_udp_t socket{};

  peer (const config &conf) noexcept;

  void start_receive () noexcept
  { }
};


struct libuv::session //{{{1
{
  uv_udp_t socket{};

  session (const endpoint &dest) noexcept;

  void start_send (packet &&p) noexcept;

  bool is_invalidated (time) const noexcept
  {
    return false;
  }
};


class relay //{{{1
{
public:

  relay (const config &conf) noexcept
    : client_{conf}
    , peer_{conf}
    , logic_{client_, peer_}
    , alloc_address_{}
  {
    libuv_call(uv_ip4_addr,
      "0.0.0.0", conf.client.port,
      (sockaddr_in *)&alloc_address_
    );
    uv_default_loop()->data = this;
  }

  int run () noexcept
  {
    return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  }

  // TODO: pool allocator
  static void alloc_buffer (uv_handle_t *, size_t, uv_buf_t *buf) noexcept
  {
    buf->len = 1024;
    buf->base = static_cast<char *>(malloc(buf->len));
  }

  static void free_buffer (const uv_buf_t *buf) noexcept
  {
    free(buf->base);
  }

  void on_client_received (
    const sockaddr *src,
    size_t nread,
    const uv_buf_t *buf,
    unsigned flags) noexcept
  {
    if (nread || ((flags & UV_UDP_PARTIAL) == UV_UDP_PARTIAL))
    {
      libuv::packet packet(*buf, nread);
      logic_.on_client_received(*src, packet);
    }
    free_buffer(buf);
  }

  void on_peer_received (
    const sockaddr *src,
    size_t nread,
    const uv_buf_t *buf,
    unsigned flags) noexcept
  {
    if (nread || ((flags & UV_UDP_PARTIAL) == UV_UDP_PARTIAL))
    {
      libuv::packet packet(*buf, nread);
      if (logic_.on_peer_received(*src, std::move(packet)))
      {
        return;
      }
    }
    free_buffer(buf);
  }

  void on_session_sent (libuv::session &session, const libuv::packet &packet)
    noexcept
  {
    logic_.on_session_sent(session, packet);
  }

  const sockaddr *alloc_address () const noexcept
  {
    return &alloc_address_;
  }


private:

  libuv::client client_;
  libuv::peer peer_;
  urn::relay<libuv, false> logic_;
  sockaddr alloc_address_;
};


} // namespace urn_libuv
