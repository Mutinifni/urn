#include <libuv/relay.hpp>
#include <string>


namespace urn_libuv {


namespace {


template <typename T>
void parse_numeric_argument (const std::string &name,
  const std::string &value,
  T &result)
{
  try
  {
    auto ull = std::stoull(value);
    if (ull <= std::numeric_limits<T>::max())
    {
      result = ull;
      return;
    }
    throw std::runtime_error(name + ": out of range (" + value + ')');
  }
  catch (const std::invalid_argument &e)
  {
    throw std::runtime_error(name + ": invalid argument (" + value + ')');
  }
}


} // namespace


config::config (int argc, const char *argv[])
{
  std::deque<std::string> args{argv + 1, argv + argc};
  for (auto i = 0u;  i < args.size();  ++i)
  {
    if (args[i] == "--threads")
    {
      parse_numeric_argument("threads", args.at(++i), threads);
    }
    else if (args[i] == "--client.port")
    {
      parse_numeric_argument("client.port", args.at(++i), client.port);
    }
    else if (args[i] == "--peer.port")
    {
      parse_numeric_argument("peer.port", args.at(++i), peer.port);
    }
    else
    {
      throw std::runtime_error("invalid flag: '" + args[i] + '\'');
    }
  }

  if (!threads)
  {
    threads = 1;
  }

  std::cout
    << "threads = " << threads
    << "\nclient.port = " << client.port
    << "\npeer.port = " << peer.port
    << '\n';
}


libuv::session::session (const endpoint &dest) noexcept
{
  auto thread = relay::this_thread();
  libuv_call(uv_udp_init, &thread->loop, &socket);
  libuv_call(uv_udp_bind,
    &socket,
    &thread->owner.alloc_address_,
    UV_UDP_REUSEADDR
  );
  libuv_call(uv_udp_connect, &socket, &dest);
}


void libuv::session::start_send (packet &&p) noexcept
{
  auto block = urn_libuv::relay::block_pool::to_block_ptr(p.base);
  block->ctl.session_send.session = this;
  block->ctl.session_send.packet = std::move(p);

  libuv_call(uv_udp_send, &block->ctl.session_send.req,
    &socket,
    &block->ctl.session_send.packet, 1,
    nullptr,
    [](uv_udp_send_t *request, int status) noexcept
    {
      die_on_error(status, "session: uv_udp_send");
      auto block = reinterpret_cast<urn_libuv::relay::block_pool::block *>(request);
      relay::this_thread()->owner.on_session_sent(
        *block->ctl.session_send.session,
        block->ctl.session_send.packet
      );
    }
  );
}


namespace {


thread_local uv_loop_t *thread_loop = nullptr;


sockaddr make_ip4_addr_any_with_port (uint16_t port)
{
  sockaddr a;
  libuv_call(uv_ip4_addr, "0.0.0.0", port, (sockaddr_in *)&a);
  return a;
}


void start_udp_listener (uv_loop_t &loop,
  uv_udp_t &socket,
  uint16_t port,
  uv_udp_recv_cb cb) noexcept
{
  constexpr auto flags = AF_INET | UV_UDP_RECVMMSG;
  libuv_call(uv_udp_init_ex, &loop, &socket, flags);

  auto addr = make_ip4_addr_any_with_port(port);
  libuv_call(uv_udp_bind, &socket,
    reinterpret_cast<const sockaddr *>(&addr),
    UV_UDP_REUSEADDR
  );

  libuv_call(uv_udp_recv_start, &socket, &relay::alloc_buffer, cb);
}


} // namespace


std::thread relay::thread::start ()
{
  libuv_call(uv_loop_init, &loop);
  loop.data = this;

  start_udp_listener(loop, client, owner.config_.client.port,
    [](uv_udp_t *handle,
      ssize_t nread,
      const uv_buf_t *buf,
      const sockaddr *src,
      unsigned flags) noexcept
    {
      die_on_error((int)nread, "client: uv_udp_recv_start");
      auto self = static_cast<relay::thread *>(handle->loop->data);
      self->owner.on_client_received(src, nread, buf, flags);
    }
  );

  start_udp_listener(loop, peer, owner.config_.peer.port,
    [](uv_udp_t *handle,
      ssize_t nread,
      const uv_buf_t *buf,
      const sockaddr *src,
      unsigned flags) noexcept
    {
      die_on_error((int)nread, "peer: uv_udp_recv_start");
      auto self = static_cast<relay::thread *>(handle->loop->data);
      self->owner.on_peer_received(src, nread, buf, flags);
    }
  );

  return std::thread(
    [this]()
    {
      thread_loop = &loop;
      uv_run(&loop, UV_RUN_DEFAULT);
    }
  );
}


relay::relay (const config &conf) noexcept
  : config_{conf}
  , alloc_address_{make_ip4_addr_any_with_port(config_.client.port)}
{ }


int relay::run () noexcept
{
  auto loop = uv_default_loop();
  loop->data = this;

  uv_timer_t statistics_timer;
  libuv_call(uv_timer_init, loop, &statistics_timer);
  statistics_timer.data = this;
  libuv_call(uv_timer_start, &statistics_timer,
    [](uv_timer_t *timer)
    {
      static_cast<relay *>(timer->data)->on_statistics_tick();
    },
    0,
    std::chrono::milliseconds{config_.statistics_print_interval}.count()
  );

  std::deque<std::thread> sys_threads;
  for (auto i = 0;  i < config_.threads;  ++i)
  {
    sys_threads.emplace_back(
      threads_.emplace_back(*this).start()
    );
  }

  auto exit_code = uv_run(loop, UV_RUN_DEFAULT);

  // never reached actually, loop above is infinite
  for (auto &thread: sys_threads)
  {
    thread.join();
  }

  return exit_code;
}


relay::thread *relay::this_thread () noexcept
{
  return static_cast<relay::thread *>(thread_loop->data);
}


void relay::alloc_buffer (uv_handle_t *, size_t, uv_buf_t *buf) noexcept
{
  auto block = this_thread()->blocks.alloc();
  buf->base = block->data;
  buf->len = sizeof(block->data);
}


void relay::release_buffer (const uv_buf_t *buf) noexcept
{
  this_thread()->blocks.release(block_pool::to_block_ptr(buf->base));
}


} // namespace urn_libuv
