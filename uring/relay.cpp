#include <arpa/inet.h>
#include <array>
#include <condition_variable>
#include <cstring>
#include <liburing.h>
#include <netdb.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <uring/relay.hpp>
#include <vector>

#define ENABLE_FIXED_BUFFERS 1
#define ENABLE_FIXED_FILE 1
#define ENABLE_SQPOLL 0

namespace urn_uring {

config::config(int argc, const char* argv[])
    : threads{static_cast<uint16_t>(std::thread::hardware_concurrency())} {
  for (int i = 1; i < argc; i++) {
    std::string_view arg(argv[i]);
    if (arg == "--threads") {
      threads = atoi(argv[++i]);
    } else if (arg == "--client.port") {
      client.port = argv[++i];
    } else if (arg == "--peer.port") {
      peer.port = argv[++i];
    } else {
      printf("unused flag: %s\n", argv[i]);
    }
  }

  if (!threads) {
    threads = 1;
  }

  std::cout << "threads = " << threads << "\nclient.port = " << client.port
            << "\npeer.port = " << peer.port << '\n';
}

namespace {

void ensure_success(int code) {
  if (code >= 0) {
    return;
  }

  fprintf(stderr, "%s\n", strerror(-code));
  abort();
}

constexpr int32_t num_events = 16; // per thread
constexpr int32_t memory_per_packet = 1024;

enum ring_event_type {
  ring_event_type_invalid = 0,
  ring_event_type_client_rx = 1,
  ring_event_type_peer_rx = 2,
  ring_event_type_peer_tx = 3,
  ring_event_type_timer = 4,

  ring_event_MAX
};

struct listen_context;

struct ring_event {
  ring_event_type type;
  int32_t index;

  union {
    struct {
      struct iovec iov;
      struct sockaddr_in address;
      struct msghdr message;
    } rx;

    __kernel_timespec timeout;
  };
};

constexpr int64_t k_statistics_timeout_ms = 5000;

struct countdown_latch {
  countdown_latch(int64_t count) : count{count} {}

  void wait() {
    std::unique_lock<std::mutex> lock(mtx);
    int64_t gen = generation;

    if (--count == 0) {
      ++generation;
      cond.notify_all();
      return;
    }

    cond.wait(lock, [&]() { return gen != generation; });
  }

  int64_t count;
  int64_t generation = 0;
  std::mutex mtx = {};
  std::condition_variable cond = {};
};

enum worker_flags { worker_flags_none = 0x0, worker_flags_report_stats = 0x01 };

struct worker_args {
  urn_uring::relay* relay = nullptr;
  struct addrinfo* local_client_address = nullptr;
  struct addrinfo* local_peer_address = nullptr;
  int32_t thread_index = 0;
  countdown_latch* startup_latch = nullptr;
  worker_flags flags = worker_flags_none;
};

struct listen_context {
  // Either FD or ID depending on the usage of registered fds
  int peer_socket = -1;
  int client_socket = -1;
  struct io_uring ring = {};
  urn_uring::relay* relay = nullptr;
  uint8_t* messages_buffer = nullptr;
  std::array<ring_event, num_events> io_events = {};
  std::vector<int32_t> free_event_ids = {};

  int peer_socket_fd = -1;
  int client_socket_fd = -1;
  int sockets[2] = {};
  worker_args worker_info = {};
  struct iovec buffers_mem = {};
};

thread_local listen_context* local_io = nullptr;

void print_address(const struct addrinfo* address) {
  char readable_ip[INET6_ADDRSTRLEN] = {0};

  if (address->ai_family == AF_INET) {
    struct sockaddr_in* ipv4 = (struct sockaddr_in*) address->ai_addr;
    inet_ntop(ipv4->sin_family, &ipv4->sin_addr, readable_ip, sizeof(readable_ip));
    printf("%s:%d\n", readable_ip, ntohs(ipv4->sin_port));
  } else if (address->ai_family == AF_INET6) {
    struct sockaddr_in6* ipv6 = (struct sockaddr_in6*) address->ai_addr;
    inet_ntop(ipv6->sin6_family, &ipv6->sin6_addr, readable_ip, sizeof(readable_ip));
    printf("[%s]:%d\n", readable_ip, ntohs(ipv6->sin6_port));
  }
}

int create_udp_socket(struct addrinfo* address_list) {
  // quickhack: only use the first address
  struct addrinfo* address = address_list;

  int socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  ensure_success(socket_fd);

  {
    int size = 4129920;
    ensure_success(setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)));
  }
  {
    int size = 4129920;
    ensure_success(setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)));
  }
  {
    int enable = 1;
    ensure_success(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
  }
  {
    int enable = 1;
    ensure_success(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)));
  }

  return socket_fd;
}

void bind_socket(int fd, struct addrinfo* address) {
  int bind_status = bind(fd, address->ai_addr, address->ai_addrlen);

  if (bind_status == 0) {
    return;
  }

  printf("error binding socket: %s\n", strerror(bind_status));

  abort();
}

ring_event* alloc_event(listen_context* context, ring_event_type type) {
  if (context->free_event_ids.size() == 0) {
    printf("[thread %d] out of free events\n", context->worker_info.thread_index);
    abort();
    return nullptr;
  }

  int32_t free_id = context->free_event_ids.back();
  context->free_event_ids.pop_back();

  ring_event* event = &context->io_events[free_id];
  event->type = type;
  event->index = free_id;
  event->rx.iov.iov_base = &context->messages_buffer[free_id * memory_per_packet];
  event->rx.iov.iov_len = memory_per_packet;
  event->rx.message.msg_name = &event->rx.address;
  event->rx.message.msg_namelen = sizeof(event->rx.address);
  event->rx.message.msg_iov = &event->rx.iov;
  event->rx.message.msg_iovlen = 1;

  return event;
}

void release_event(listen_context* context, ring_event* ev) {
  context->free_event_ids.push_back(ev->index);
}

void listen_context_initialize(listen_context* io, worker_args args) {
  struct io_uring_params params;
  memset(&params, 0, sizeof(params));

  if (ENABLE_SQPOLL) {
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 1000;
    printf("[thread %d] using SQPOLL\n", args.thread_index);
  }

  ensure_success(io_uring_queue_init_params(4096, &io->ring, &params));

  io->worker_info = args;
  io->relay = args.relay;
  io->client_socket_fd = create_udp_socket(args.local_client_address);
  io->peer_socket_fd = create_udp_socket(args.local_peer_address);
  printf("[thread %d] sockets: %d %d\n", args.thread_index, io->client_socket_fd,
         io->peer_socket_fd);

  if (ENABLE_FIXED_FILE) {
    io->sockets[0] = io->peer_socket_fd;
    io->sockets[1] = io->client_socket_fd;
    ensure_success(io_uring_register_files(&io->ring, io->sockets, 2));
    io->peer_socket = 0;
    io->client_socket = 1;
    printf("[thread %d] using fixed files\n", args.thread_index);
  } else {
    io->client_socket = io->client_socket_fd;
    io->peer_socket = io->peer_socket_fd;
  }

  io->messages_buffer = (uint8_t*) calloc(num_events, memory_per_packet);

  io->free_event_ids.reserve(num_events);
  for (int32_t i = 0; i < num_events; i++) {
    io->free_event_ids.push_back(i);
  }

  if (ENABLE_FIXED_BUFFERS) {
    io->buffers_mem.iov_base = io->messages_buffer;
    io->buffers_mem.iov_len = num_events * memory_per_packet;
    ensure_success(io_uring_register_buffers(&io->ring, &io->buffers_mem, 1));
    printf("[thread %d] using fixed buffers\n", args.thread_index);
  }
}

struct addrinfo* bindable_address(const char* port) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo* addr = nullptr;
  ensure_success(getaddrinfo(nullptr, port, &hints, &addr));

  return addr;
}

__kernel_timespec create_timeout(int64_t milliseconds) {
  __kernel_timespec spec;
  spec.tv_sec = milliseconds / 1000;
  spec.tv_nsec = (milliseconds % 1000) * 1000000;
  return spec;
}

#if ENABLE_FIXED_FILE
void enable_fixed_file(struct io_uring_sqe* sqe) { sqe->flags |= IOSQE_FIXED_FILE; }
#else
void enable_fixed_file(struct io_uring_sqe*) {}
#endif

#if ENABLE_FIXED_BUFFERS
void enable_fixed_buffers(struct io_uring_sqe* sqe) {
  // One huge slab is registered and then partitioned manually
  sqe->buf_index = 0;
}
#else
void enable_fixed_buffers(struct io_uring_sqe*) {}
#endif

void add_recvmsg(struct io_uring* ring, ring_event* ev, int32_t socket_id) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (!sqe)
    abort();
  io_uring_prep_recvmsg(sqe, socket_id, &ev->rx.message, 0);
  io_uring_sqe_set_data(sqe, ev);
  enable_fixed_file(sqe);
  enable_fixed_buffers(sqe);
}

void add_sendmsg(struct io_uring* ring, ring_event* ev, int32_t socket_id) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (!sqe)
    abort();
  io_uring_prep_sendmsg(sqe, socket_id, &ev->rx.message, 0);
  io_uring_sqe_set_data(sqe, ev);
  enable_fixed_file(sqe);
  enable_fixed_buffers(sqe);
}

void add_timeout(struct io_uring* ring, ring_event* ev, int64_t milliseconds) {
  ev->timeout = create_timeout(milliseconds);
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (!sqe)
    abort();
  io_uring_prep_timeout(sqe, &ev->timeout, 0, 0);
  io_uring_sqe_set_data(sqe, ev);
}

volatile sig_atomic_t has_sigint = 0;

void worker(worker_args args) {
  auto iop = std::make_unique<listen_context>();
  listen_context* io = iop.get();
  local_io = io;
  listen_context_initialize(io, args);

  struct io_uring* ring = &io->ring;

  args.startup_latch->wait();

  bind_socket(io->client_socket_fd, args.local_client_address);
  bind_socket(io->peer_socket_fd, args.local_peer_address);

  io->relay->on_thread_start(args.thread_index);

  {
    ring_event* ev = alloc_event(io, ring_event_type_client_rx);
    add_recvmsg(ring, ev, io->client_socket);
  }

  {
    ring_event* ev = alloc_event(io, ring_event_type_peer_rx);
    add_recvmsg(ring, ev, io->peer_socket);
  }

  if (args.flags & worker_flags_report_stats) {
    ring_event* ev = alloc_event(io, ring_event_type_timer);
    add_timeout(ring, ev, k_statistics_timeout_ms);
  }

  for (;;) {
    io_uring_submit_and_wait(ring, 1);

    struct io_uring_cqe* cqe;
    uint32_t head;
    uint32_t count = 0;

    io_uring_for_each_cqe(ring, head, cqe) {
      ++count;

      ring_event* event = (ring_event*) cqe->user_data;

      switch (event->type) {
        case ring_event_type_peer_rx: {
          if (cqe->res < 0) {
            printf("thread [%d] peer rx %s\n", args.thread_index, strerror(-cqe->res));
            abort();
          }
          const msghdr* message = &event->rx.message;
          uring::packet packet{(const std::byte*) message->msg_iov->iov_base, (uint32_t) cqe->res};
          io->relay->on_peer_received(uring::endpoint(event->rx.address, io), packet);

          release_event(io, event);

          ring_event* rx_event = alloc_event(io, ring_event_type_peer_rx);
          add_recvmsg(ring, rx_event, io->peer_socket);
          break;
        }
        case ring_event_type_peer_tx: {
          uring::packet packet{(const std::byte*) event->rx.message.msg_iov->iov_base,
                               (uint32_t) cqe->res};
          io->relay->on_session_sent(packet);
          release_event(io, event);
          break;
        }
        case ring_event_type_client_rx: {
          if (cqe->res < 0) {
            printf("thread [%d] client rx: %s\n", args.thread_index, strerror(-cqe->res));
            abort();
          }

          const msghdr* message = &event->rx.message;
          io->relay->on_client_received(
              uring::endpoint(event->rx.address, io),
              uring::packet((const std::byte*) message->msg_iov->iov_base, cqe->res));
          release_event(io, event);

          ring_event* rx_event = alloc_event(io, ring_event_type_client_rx);
          add_recvmsg(ring, rx_event, io->client_socket);
          break;
        }
        case ring_event_type_timer: {
          io->relay->on_statistics_tick();
          add_timeout(ring, event, k_statistics_timeout_ms); // reuse
          break;
        }
        default: {
          printf("unhandled ev\n");
          release_event(io, event);
          break;
        }
      }
    }
    io_uring_cq_advance(ring, count);

    if (has_sigint) {
      break;
    }
  }

  printf("%d worker exiting \n", io->worker_info.thread_index);

  io_uring_queue_exit(ring);
}

void handle_sigint(int) { has_sigint = 1; }

} // namespace

relay::relay(const urn_uring::config& conf) noexcept
    : config_{conf}, logic_{config_.threads, client_, peer_} {}

int relay::run() noexcept {
  if (ENABLE_SQPOLL) {
    if (geteuid() != 0) {
      printf("SQPOLL needs sudo\n");
      return 1;
    }
  }

  signal(SIGINT, handle_sigint);

  int32_t thread_count = std::max(uint16_t(1), config_.threads);

  struct addrinfo* local_client_address = bindable_address("3478");
  struct addrinfo* local_peer_address = bindable_address("3479");
  print_address(local_client_address);
  print_address(local_peer_address);

  countdown_latch latch(thread_count);

  auto make_worker_args = [=, &latch](int32_t thread_index) {
    worker_args args;
    args.relay = this;
    args.local_client_address = local_client_address;
    args.local_peer_address = local_peer_address;
    args.thread_index = thread_index;
    args.startup_latch = &latch;
    return args;
  };

  std::vector<std::thread> worker_threads;

  for (int32_t i = 1; i < thread_count; i++) {
    worker_threads.emplace_back(worker, make_worker_args(i));
  }

  worker_args main_thread_args = make_worker_args(0);
  main_thread_args.flags = worker_flags_report_stats;

  worker(main_thread_args);

  printf("joining\n");

  for (auto& t : worker_threads) {
    t.join();
  }

  printf("joined\n");

  return 0;
}

void uring::session::start_send(const uring::packet& packet) noexcept {
  listen_context* io = local_io;

  ring_event* tx_event = alloc_event(io, ring_event_type_peer_tx);
  tx_event->rx.address = client_endpoint.address;
  memcpy(tx_event->rx.iov.iov_base, packet.data(), packet.size());
  tx_event->rx.iov.iov_len = packet.size();
  add_sendmsg(&io->ring, tx_event, io->client_socket);
}

} // namespace urn_uring
