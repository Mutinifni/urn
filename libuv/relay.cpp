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
    if (ull <= (std::numeric_limits<T>::max)())
    {
      result = static_cast<T>(ull);
      return;
    }
    throw std::runtime_error(name + ": out of range (" + value + ')');
  }
  catch (const std::invalid_argument &)
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


namespace {


//
// Under Linux with UV_UDP_RECVMMSG, each io_buf can be split into many chunks
// that can't be freed on own
//

struct io_buf
{
  union
  {
    struct
    {
      uv_udp_send_t req{};
      libuv::packet packet{};
      libuv::session *session{};
    }  send{};
  } ctl{};

  urn::intrusive_stack_hook<io_buf> next{};
  char data[2 * 64 * 1024];
};


struct io_buf_pool
{
  urn::intrusive_stack<&io_buf::next> pool{};

  io_buf *alloc () noexcept
  {
    auto b = pool.try_pop();
    if (!b)
    {
      b = new io_buf;
      if (!b)
      {
        die_on_error(UV_ENOMEM, "io_buf_pool::alloc", __FILE__, __LINE__);
      }
    }
    std::cout << "alloc " << (void*)b << '\n';
    return b;
  }

  void release (io_buf *b) noexcept
  {
    std::cout << "release " << (void*)b << '\n';
    pool.push(b);
  }

  static io_buf *to_io_mem_ptr (char *base) noexcept
  {
    return reinterpret_cast<io_buf *>(
      base + sizeof(io_buf::data) - sizeof(io_buf)
    );
  }
};


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
  constexpr auto udp_flags = AF_INET | (have_mmsg ? UV_UDP_RECVMMSG : 0);
  libuv_call(uv_udp_init_ex, &loop, &socket, udp_flags);

#if defined(SO_REUSEPORT)

  uv_os_fd_t fd;
  libuv_call(uv_fileno, reinterpret_cast<uv_handle_t *>(&socket), &fd);
  int enable = 1;
  die_on_error(
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)),
    "setsockopt",
    __FILE__,
    __LINE__
  );

#endif

  auto addr = make_ip4_addr_any_with_port(port);
  libuv_call(uv_udp_bind, &socket,
    reinterpret_cast<const sockaddr *>(&addr),
    UV_UDP_REUSEADDR
  );

  libuv_call(uv_udp_recv_start, &socket, &relay::alloc_buffer, cb);
}


struct thread
{
  relay &owner;
  uv_loop_t loop{};
  uv_udp_t client{}, peer{};
  io_buf_pool io_bufs{};
  std::thread sys_thread{};

  thread (relay &owner) noexcept
    : owner(owner)
  {}

  void start ();

  void join ()
  {
    sys_thread.join();
  }
};


thread_local thread *this_thread = nullptr;


void thread::start ()
{
  libuv_call(uv_loop_init, &loop);
  loop.data = this;

  start_udp_listener(loop, client, owner.config().client.port,
    [](uv_udp_t *handle,
      ssize_t nread,
      const uv_buf_t *buf,
      const sockaddr *src,
      unsigned flags) noexcept
    {
      die_on_error((int)nread, "client: uv_udp_recv_start", __FILE__, __LINE__);
      auto self = static_cast<thread *>(handle->loop->data);
      self->owner.on_client_received(src, nread, buf, flags);
    }
  );

  start_udp_listener(loop, peer, owner.config().peer.port,
    [](uv_udp_t *handle,
      ssize_t nread,
      const uv_buf_t *buf,
      const sockaddr *src,
      unsigned flags) noexcept
    {
      die_on_error((int)nread, "peer: uv_udp_recv_start", __FILE__, __LINE__);
      auto self = static_cast<thread *>(handle->loop->data);
      self->owner.on_peer_received(src, nread, buf, flags);
    }
  );

  sys_thread = std::thread(
    [this]()
    {
      this_thread = this;
      uv_run(&loop, UV_RUN_DEFAULT);
    }
  );
}


} // namespace


relay::relay (const urn_libuv::config &conf) noexcept
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

  std::deque<thread> threads;
  for (auto i = 0;  i < config_.threads;  ++i)
  {
    threads.emplace_back(*this).start();
  }

  auto exit_code = uv_run(loop, UV_RUN_DEFAULT);

  // never reached actually, loop above is infinite
  for (auto &thread: threads)
  {
    thread.join();
  }

  return exit_code;
}


void relay::alloc_buffer (uv_handle_t *, size_t, uv_buf_t *buf) noexcept
{
  auto b = this_thread->io_bufs.alloc();
  buf->base = b->data;
  buf->len = sizeof(b->data);
}


void relay::release_buffer (const uv_buf_t *buf, unsigned uv_flags) noexcept
{
  if (uv_flags & UV_UDP_MMSG_CHUNK)
  {
    return;
  }
  this_thread->io_bufs.release(io_buf_pool::to_io_mem_ptr(buf->base));
}


libuv::session::session (const endpoint &dest) noexcept
{
  libuv_call(uv_udp_init, &this_thread->loop, &socket);
  libuv_call(uv_udp_bind,
    &socket,
    &this_thread->owner.alloc_address(),
    UV_UDP_REUSEADDR
  );
  libuv_call(uv_udp_connect, &socket, &dest);
}


void libuv::session::start_send (packet &&p) noexcept
{
  auto b = io_buf_pool::to_io_mem_ptr(p.base);
  b->ctl.send.session = this;
  b->ctl.send.packet = std::move(p);

  libuv_call(uv_udp_send, &b->ctl.send.req,
    &socket,
    &b->ctl.send.packet, 1,
    nullptr,
    [](uv_udp_send_t *request, int status) noexcept
    {
      die_on_error(status, "session: uv_udp_send", __FILE__, __LINE__);
      auto b = reinterpret_cast<io_buf *>(request);
      this_thread->owner.on_session_sent(*b->ctl.send.session, b->ctl.send.packet);
    }
  );
}


} // namespace urn_libuv
