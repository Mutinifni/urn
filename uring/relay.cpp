#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <thread>
#include <inttypes.h>
#include <liburing.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <uring/relay.hpp>
#include <vector>

namespace urn_uring {

config::config(int argc, const char* argv[])
    : threads{static_cast<uint16_t>(std::thread::hardware_concurrency())} {
  for (int i = 0; i < argc; i++) {
    std::string_view arg(argv[i]);
    if (arg == "--threads") {
      threads = atoi(argv[i]);
    } else if (arg == "--client.port") {
      client.port = argv[i];
    } else if (arg == "--peer.port") {
      peer.port = argv[i];
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

  fprintf(stderr, "%s\n", strerror(code));
  abort();
}

} // namespace

const size_t num_events = 16;
const size_t memory_per_packet = 1024;

enum ring_event_type {
  ring_event_type_invalid = 0,
  ring_event_type_client_rx = 1,
  ring_event_type_peer_rx = 2,
  ring_event_type_peer_tx = 3,
  ring_event_type_timer = 4
};

struct listen_context;

struct ring_event {
  ring_event_type type;
  uint32_t index;
  listen_context* io;

  struct {
    struct iovec iov;
    struct sockaddr_storage address;
    struct msghdr message;
  } rx;
};

struct listen_context {
  struct io_uring ring = {};
  urn_uring::relay* relay = nullptr;
  int client_socket_fd = -1;
  int peer_socket_fd = -1;
  uint8_t* messages_buffer = nullptr;
  std::vector<uint32_t> free_event_ids = {};
  std::array<ring_event, num_events> io_events = {};
};

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
  struct addrinfo* address = address_list;
  for (; address != nullptr; address = address->ai_next) {
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

    int bind_status = bind(socket_fd, address->ai_addr, address->ai_addrlen);

    if (bind_status == 0) {
      print_address(address);
      return socket_fd;
    }

    close(socket_fd);
    abort();
  }

  return -1;
}

ring_event* get_free_event(listen_context* context, ring_event_type type) {
  if (context->free_event_ids.size() == 0) {
    return nullptr;
  }

  uint32_t free_id = context->free_event_ids.back();
  context->free_event_ids.pop_back();

  ring_event* event = &context->io_events[free_id];
  memset(event, 0, sizeof(*event));
  event->type = type;
  event->index = free_id;
  event->io = context;
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

void listen_context_initialize(listen_context* context, struct addrinfo* client_address_list,
                               struct addrinfo* peer_address_list, urn_uring::relay* relay) {
  struct io_uring_params params;
  memset(&params, 0, sizeof(params));
  // params.flags |= IORING_SETUP_SQPOLL;
  // params.sq_thread_idle = 2000;

  ensure_success(io_uring_queue_init_params(num_events * 2, &context->ring, &params));

  context->relay = relay;
  context->client_socket_fd = create_udp_socket(client_address_list);
  context->peer_socket_fd = create_udp_socket(peer_address_list);

  context->messages_buffer = (uint8_t*) calloc(num_events, memory_per_packet);

  context->free_event_ids.reserve(num_events);
  for (uint32_t i = 0; i < num_events; i++) {
    context->free_event_ids.push_back(i);
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

relay::relay(const urn_uring::config& conf) noexcept
    : config_{conf}, logic_{config_.threads, client_, peer_} {}

int relay::run() noexcept {
  struct addrinfo* local_client_address = bindable_address("3478");
  struct addrinfo* local_peer_address = bindable_address("3479");

  listen_context* io = new listen_context();
  listen_context_initialize(io, local_client_address, local_peer_address, this);
  printf("%d %d\n", io->client_socket_fd, io->peer_socket_fd);

  // ensure_success(io_uring_register_buffers(&ring, io.rx_vectors.data(), num_messages));

  // int sockets[2] = {io.client_socket_fd, io.peer_socket_fd};
  // ensure_success(io_uring_register_files(&ring, sockets, 2));

  logic_.on_thread_start(0);

  struct io_uring* ring = &io->ring;

  {
    ring_event* ev = get_free_event(io, ring_event_type_client_rx);
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_recvmsg(sqe, io->client_socket_fd, &ev->rx.message, 0);
    io_uring_sqe_set_data(sqe, ev);
  }

  {
    ring_event* ev = get_free_event(io, ring_event_type_peer_rx);
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_recvmsg(sqe, io->peer_socket_fd, &ev->rx.message, 0);
    io_uring_sqe_set_data(sqe, ev);
  }

  __kernel_timespec timeout = create_timeout(5000);
  { 
    ring_event* ev = get_free_event(io, ring_event_type_timer);
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_timeout(sqe, &timeout, 0, 0);
    io_uring_sqe_set_data(sqe, ev);
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
          const msghdr* message = &event->rx.message;
          uring::packet packet{(const std::byte*) message->msg_iov->iov_base, (uint32_t) cqe->res};
          io->relay->on_peer_received(uring::endpoint(event->rx.address, io), packet);

          {
            ring_event* ev = get_free_event(io, ring_event_type_peer_rx);
            struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recvmsg(sqe, io->peer_socket_fd, &ev->rx.message, 0);
            io_uring_sqe_set_data(sqe, ev);
          }
          break;
        }
        case ring_event_type_client_rx: {
          const msghdr* message = &event->rx.message;
          io->relay->on_client_received(
              uring::endpoint(event->rx.address, io),
              uring::packet((const std::byte*) message->msg_iov->iov_base, cqe->res));

          {
            ring_event* ev = get_free_event(io, ring_event_type_client_rx);
            struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
            io_uring_prep_recvmsg(sqe, io->client_socket_fd, &ev->rx.message, 0);
            io_uring_sqe_set_data(sqe, ev);
          }
          break;
        }
        case ring_event_type_timer: {
          on_statistics_tick();
          ring_event* ev = get_free_event(io, ring_event_type_timer);
          struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
          io_uring_prep_timeout(sqe, &timeout, 0, 0);
          io_uring_sqe_set_data(sqe, ev);
          break;
        }
        default:
          break;
      }

      release_event(io, event);
    }
    io_uring_cq_advance(ring, count);
  }

  return 0;
}

void uring::session::start_send(const uring::packet& packet) noexcept {
  listen_context* io = (listen_context*) client_endpoint.user_data;
  ring_event* ev = get_free_event(io, ring_event_type_peer_tx);
  ev->rx.address = client_endpoint.address;
  memcpy(ev->rx.iov.iov_base, packet.data(), packet.size());
  ev->rx.iov.iov_len = packet.size();

  struct io_uring_sqe* sqe = io_uring_get_sqe(&io->ring);
  io_uring_prep_sendmsg(sqe, io->client_socket_fd, &ev->rx.message, 0);
  io_uring_sqe_set_data(sqe, ev);

  io->relay->on_session_sent(*this, packet);
}

} // namespace urn_uring
