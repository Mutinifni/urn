#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mmsg/relay.hpp>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

namespace urn_mmsg {

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

void set_nonblocking(int sfd) {
  int flags = fcntl(sfd, F_GETFL, 0);
  if (flags == -1) {
    abort();
  }

  flags |= O_NONBLOCK;

  if (fcntl(sfd, F_SETFL, flags) == -1) {
    abort();
  }
}

} // namespace

const size_t num_messages = 2048;
const size_t memory_per_packet = 1024;

struct tx_submission {
  tx_submission() = default;
  sockaddr_storage to;
  struct iovec iovec;
  struct mmsghdr mmsg;
};

void tx_submission_init(tx_submission* tx, sockaddr_storage to, const std::byte* content,
                        size_t length) {
  tx->to = to;

  tx->iovec.iov_base = (void*) content;
  tx->iovec.iov_len = length;

  tx->mmsg.msg_len = 0;
  tx->mmsg.msg_hdr.msg_name = (void*) &tx->to;
  tx->mmsg.msg_hdr.msg_namelen = sizeof(to);
  tx->mmsg.msg_hdr.msg_iov = &tx->iovec;
  tx->mmsg.msg_hdr.msg_iovlen = 1;
}

struct listen_context {
  int outgoing_message_count = 0;
  struct addrinfo* address_list = nullptr;
  std::array<sockaddr_storage, num_messages> remote_addresses = {};
  std::array<struct iovec, num_messages> rx_vectors = {};
  std::array<struct mmsghdr, num_messages> rx_messages = {};
  std::array<tx_submission, num_messages> tx_submissions = {};
  int socket_fd = -1;
  uint8_t* messages_buffer = nullptr;
  urn_mmsg::relay* relay = nullptr;
};

void listen_context_initialize(listen_context* context, struct addrinfo* address_list,
                               urn_mmsg::relay* relay) {
  context->address_list = address_list;
  context->relay = relay;

  struct addrinfo* address = address_list;
  for (; address != nullptr; address = address->ai_next) {
    context->socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    ensure_success(context->socket_fd);

    set_nonblocking(context->socket_fd);

    {
      int rcvSize = 0;
      socklen_t siz = sizeof(rcvSize);
      getsockopt(context->socket_fd, SOL_SOCKET, SO_RCVBUF, &rcvSize, &siz);
      printf("receive buffer size: %d\n", rcvSize);
    }
    {
      int size = 4129920;
      ensure_success(setsockopt(context->socket_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)));
    }
    {
      int size = 4129920;
      ensure_success(setsockopt(context->socket_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)));
    }
    {
      int rcvSize = 0;
      socklen_t siz = sizeof(rcvSize);
      getsockopt(context->socket_fd, SOL_SOCKET, SO_RCVBUF, &rcvSize, &siz);
      printf("receive buffer size: %d\n", rcvSize);
    }
    {
      int enable = 1;
      ensure_success(
          setsockopt(context->socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
    }
    {
      int enable = 1;
      ensure_success(
          setsockopt(context->socket_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)));
    }

    int bind_status = bind(context->socket_fd, address->ai_addr, address->ai_addrlen);
    if (bind_status == 0) {
      print_address(address);
      break;
    }

    close(context->socket_fd);
    abort();
  }

  context->messages_buffer = (uint8_t*) calloc(num_messages, memory_per_packet);

  for (size_t i = 0; i < context->rx_vectors.max_size(); i++) {
    const size_t mem_offset = i * memory_per_packet;
    context->rx_vectors[i].iov_base = context->messages_buffer + mem_offset;
    context->rx_vectors[i].iov_len = memory_per_packet;
  }
}

void listen_context_begin_frame(listen_context* context) {
  context->outgoing_message_count = 0;

  std::memset(&context->remote_addresses[0], 0, sizeof(context->remote_addresses));

  for (auto& v : context->rx_vectors) {
    if (v.iov_len != memory_per_packet) {
      abort();
    }
  }

  for (size_t i = 0; i < context->rx_messages.size(); i++) {
    struct mmsghdr* mmsg_header = &context->rx_messages[i];
    mmsg_header->msg_len = 0;

    struct msghdr* header = &mmsg_header->msg_hdr;
    header->msg_name = &context->remote_addresses[i];
    header->msg_namelen = sizeof(sockaddr_storage);
    header->msg_iov = &context->rx_vectors[i];
    header->msg_iovlen = 1;
    header->msg_control = nullptr;
    header->msg_controllen = 0;
    header->msg_flags = 0;
  }

  std::memset(&context->tx_submissions[0], 0, sizeof(context->tx_submissions));
}

struct addrinfo* bindable_address(const char* port) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo* addr = nullptr;
  ensure_success(getaddrinfo(nullptr, port, &hints, &addr));

  return addr;
}

void run_peer_receive_frame(listen_context* context) {
  int messages_received = recvmmsg(context->socket_fd, context->rx_messages.data(),
                                   context->rx_messages.max_size(), 0, nullptr);
  ensure_success(messages_received);

  for (int i = 0; i < messages_received; i++) {
    const struct mmsghdr* mmsg_header = &context->rx_messages[i];
    const struct msghdr* header = &mmsg_header->msg_hdr;
    const struct iovec* iov = &header->msg_iov[0];
    struct sockaddr_storage* from = (struct sockaddr_storage*) header->msg_name;
    mmsg::packet packet{(const std::byte*) iov->iov_base, mmsg_header->msg_len};
    context->relay->on_peer_received(mmsg::endpoint(*from, context), packet);
  }
}

void HexDump(const void* src, size_t len) {
  const uint8_t* bytes = (const uint8_t*) src;
  for (size_t i = 0; i < len; i++) {
    if (i % 8 == 0)
      printf("%04x ", uint32_t(i));

    printf("%02x ", bytes[i]);

    if ((i + 1) % 8 == 0)
      printf("\n");
  }
  printf("\n");
}

void run_client_receive_frame(listen_context* context) {
  int messages_received = recvmmsg(context->socket_fd, context->rx_messages.data(),
                                   context->rx_messages.size(), 0, nullptr);
  ensure_success(messages_received);

  for (int i = 0; i < messages_received; i++) {
    const struct mmsghdr* mmsg_header = &context->rx_messages[i];
    const struct msghdr* header = &mmsg_header->msg_hdr;
    const struct iovec* iov = &header->msg_iov[0];
    struct sockaddr_storage* from = (struct sockaddr_storage*) header->msg_name;
    context->relay->on_client_received(
        mmsg::endpoint(*from, context),
        mmsg::packet((const std::byte*) iov->iov_base, mmsg_header->msg_len));
  }
}

void run_client_send_frame(listen_context* context) {
  struct mmsghdr headers[num_messages];

  for (int i = 0; i < context->outgoing_message_count; i++) {
    headers[i] = context->tx_submissions[i].mmsg;
  }

  int remaining = context->outgoing_message_count;
  int offset = 0;

  while (remaining > 0) {
    int sent = sendmmsg(context->socket_fd, &headers[offset], context->outgoing_message_count, 0);

    if (sent < 0) {
      if (errno == EINTR || errno == EWOULDBLOCK) {
        continue;
      }

      printf("sendmmsg error: %s\n", strerror(errno));
      abort();
    }

    remaining -= sent;
    offset += sent;
  }
}

relay::relay(const urn_mmsg::config& conf) noexcept
    : config_{conf}, logic_{config_.threads, client_, peer_} {}

int relay::run() noexcept {
  struct addrinfo* local_client_address = bindable_address("3478");
  struct addrinfo* local_peer_address = bindable_address("3479");

  listen_context client_context;
  listen_context_initialize(&client_context, local_client_address, this);

  listen_context peer_context;
  listen_context_initialize(&peer_context, local_peer_address, this);

  int statistics_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
  ensure_success(statistics_timer_fd);

  struct timespec timespec;
  timespec.tv_sec = config_.statistics_print_interval.count();
  timespec.tv_nsec = 0;
  struct itimerspec timer;
  timer.it_interval = timespec;
  timer.it_value = timespec;

  ensure_success(timerfd_settime(statistics_timer_fd, TFD_TIMER_ABSTIME, &timer, nullptr));

  logic_.on_thread_start(0);

  for (;;) {
    listen_context_begin_frame(&client_context);
    listen_context_begin_frame(&peer_context);

    const nfds_t num_fds = 3;
    struct pollfd poll_entries[num_fds];

    struct pollfd* peer_poll_entry = &poll_entries[0];
    struct pollfd* client_poll_entry = &poll_entries[1];
    struct pollfd* timer_poll_entry = &poll_entries[2];

    peer_poll_entry->fd = peer_context.socket_fd;
    peer_poll_entry->events = POLLIN;
    peer_poll_entry->revents = 0;

    client_poll_entry->fd = client_context.socket_fd;
    client_poll_entry->events = POLLIN;
    client_poll_entry->revents = 0;

    timer_poll_entry->fd = statistics_timer_fd;
    timer_poll_entry->events = POLLIN;
    timer_poll_entry->revents = 0;

    poll(poll_entries, num_fds, -1);

    if ((client_poll_entry->revents & POLLIN) != 0) {
      run_client_receive_frame(&client_context);
    }

    if ((peer_poll_entry->revents & POLLIN) != 0) {
      run_peer_receive_frame(&peer_context);
    }

    if ((timer_poll_entry->revents & POLLIN) != 0) {
      uint64_t exp = 0;
      ensure_success(read(statistics_timer_fd, &exp, sizeof(exp)));
      on_statistics_tick();
    }

    run_client_send_frame(&client_context);
  }

  return 0;
}

void mmsg::session::start_send(const mmsg::packet& packet) noexcept {
  listen_context* context = (listen_context*) client_endpoint.user_data;

  tx_submission_init(&context->tx_submissions[context->outgoing_message_count],
                     client_endpoint.address, packet.data(), packet.size());

  context->outgoing_message_count++;

  context->relay->on_session_sent(*this, packet);
}

} // namespace urn_mmsg
