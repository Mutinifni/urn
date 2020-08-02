#pragma once

/**
 * \file libuv/relay.hpp
 */

#include <urn/relay.hpp>
#include <uv.h>


// TODO: http://docs.libuv.org/en/v1.x/udp.html#c.uv_udp_using_recvmmsg


namespace urn_libuv {


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

  relay (const config &conf) noexcept;


  int run () noexcept
  {
    auto loop = uv_default_loop();
    loop->data = this;
    return uv_run(loop, UV_RUN_DEFAULT);
  }


  static void log_packet (const char *prefix,
    const sockaddr *src,
    const uv_buf_t &buf) noexcept
  {
    char name[64];
    auto in = reinterpret_cast<const sockaddr_in *>(src);
    uv_ip4_name(in, name, sizeof(name));
    printf("%s(%s:%hu, '%*.*s')\n",
      prefix,
      name,
      ntohs(in->sin_port),
      (int)buf.len, (int)buf.len, buf.base
    );
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
      log_packet("on_client_received", src, packet);
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
      log_packet("on_peer_received", src, packet);
      if (logic_.on_peer_received(*src, std::move(packet)))
      {
        // on successful forwarding, session took packet ownership
        return;
      }
    }
    free_buffer(buf);
  }


  void on_session_sent (libuv::session &session, const libuv::packet &packet)
    noexcept
  {
    printf("session_sent(%*.*s)\n",
      (int)packet.size(),
      (int)packet.size(),
      (char *)packet.data()
    );
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
