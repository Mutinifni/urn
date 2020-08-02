#include <libuv/relay.hpp>
#include <cstdlib>
#include <iostream>


namespace urn_libuv {


namespace {

constexpr bool log_calls = false;

inline void die_on_error (int code, const char *fn)
{
  if constexpr (log_calls)
  {
    std::cout << fn << '=' << code << '\n';
  }

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

#define call(F, ...) die_on_error(F(__VA_ARGS__), #F)


void start_udp_listener (uv_udp_t &socket, uint16_t port, uv_udp_recv_cb cb)
{
  call(uv_udp_init, uv_default_loop(), &socket);

  sockaddr_in addr;
  call(uv_ip4_addr, "0.0.0.0", port, &addr);

  call(uv_udp_bind, &socket,
    reinterpret_cast<const sockaddr *>(&addr),
    UV_UDP_REUSEADDR
  );

  call(uv_udp_recv_start, &socket, &urn_libuv::relay::alloc_buffer, cb);
}


} // namespace


libuv::client::client (const config &conf) noexcept
{
  start_udp_listener(socket, conf.client.port,
    [](uv_udp_t *handle,
      ssize_t nread,
      const uv_buf_t *buf,
      const sockaddr *src,
      unsigned flags) noexcept
    {
      die_on_error((int)nread, "client: uv_udp_recv_start");
      auto relay = static_cast<urn_libuv::relay *>(handle->loop->data);
      relay->on_client_received(src, nread, buf, flags);
    }
  );
}


libuv::peer::peer (const config &conf) noexcept
{
  start_udp_listener(socket, conf.peer.port,
    [](uv_udp_t *handle,
      ssize_t nread,
      const uv_buf_t *buf,
      const sockaddr *src,
      unsigned flags) noexcept
    {
      die_on_error((int)nread, "peer: uv_udp_recv_start");
      auto relay = static_cast<urn_libuv::relay *>(handle->loop->data);
      relay->on_peer_received(src, nread, buf, flags);
    }
  );
}


libuv::session::session (const endpoint &dest) noexcept
{
  call(uv_udp_init, uv_default_loop(), &socket);

  call(uv_udp_bind, &socket,
    static_cast<urn_libuv::relay *>(uv_default_loop()->data)->alloc_address(),
    UV_UDP_REUSEADDR
  );

  call(uv_udp_connect, &socket, &dest);
}


void libuv::session::start_send (packet &&p) noexcept
{
  // TODO: pool allocator

  struct udp_fwd_req_t
  {
    uv_udp_send_t req;
    session *s;
    packet p;
  };

  auto req = static_cast<udp_fwd_req_t *>(malloc(sizeof(udp_fwd_req_t)));
  req->s = this;
  req->p = std::move(p);

  call(uv_udp_send, reinterpret_cast<uv_udp_send_t *>(req),
    &socket,
    &req->p, 1,
    nullptr,
    [](uv_udp_send_t *handle, int status) noexcept
    {
      die_on_error(status, "session: uv_udp_send");
      auto relay = static_cast<urn_libuv::relay *>(uv_default_loop()->data);
      auto req = reinterpret_cast<udp_fwd_req_t *>(handle);
      relay->on_session_sent(*req->s, req->p);
      relay->free_buffer(&req->p);
      free(req);
    }
  );
}


relay::relay (const config &conf) noexcept
  : client_{conf}
  , peer_{conf}
  , logic_{client_, peer_}
  , alloc_address_{}
{
  call(uv_ip4_addr, "0.0.0.0", conf.client.port, (sockaddr_in *)&alloc_address_);
}


} // namespace urn_libuv
