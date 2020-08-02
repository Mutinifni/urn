#include <libuv/relay.hpp>


namespace urn_libuv {


namespace {


void start_udp_listener (uv_udp_t &socket, uint16_t port, uv_udp_recv_cb cb)
  noexcept
{
  libuv_call(uv_udp_init, uv_default_loop(), &socket);

  sockaddr_in addr;
  libuv_call(uv_ip4_addr, "0.0.0.0", port, &addr);

  libuv_call(uv_udp_bind, &socket,
    reinterpret_cast<const sockaddr *>(&addr),
    UV_UDP_REUSEADDR
  );

  libuv_call(uv_udp_recv_start, &socket, &urn_libuv::relay::alloc_buffer, cb);
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
  auto loop = uv_default_loop();
  auto relay = static_cast<urn_libuv::relay *>(loop->data);

  libuv_call(uv_udp_init, loop, &socket);
  libuv_call(uv_udp_bind, &socket, relay->alloc_address(), UV_UDP_REUSEADDR);
  libuv_call(uv_udp_connect, &socket, &dest);
}


void libuv::session::start_send (packet &&p) noexcept
{
  auto block = urn_libuv::relay::block_pool::base_to_block_ptr(p.base);
  block->ctl.session_send.session = this;
  block->ctl.session_send.packet = std::move(p);

  libuv_call(uv_udp_send, &block->ctl.session_send.req,
    &socket,
    &block->ctl.session_send.packet, 1,
    nullptr,
    [](uv_udp_send_t *handle, int status) noexcept
    {
      die_on_error(status, "session: uv_udp_send");
      auto relay = static_cast<urn_libuv::relay *>(uv_default_loop()->data);
      auto block = reinterpret_cast<urn_libuv::relay::block_pool::block *>(handle);
      relay->on_session_sent(
        *block->ctl.session_send.session,
        block->ctl.session_send.packet
      );
    }
  );
}


} // namespace urn_libuv
