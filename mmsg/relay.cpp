#include <arpa/inet.h>
#include <common/latch.h>
#include <cstring>
#include <deque>
#include <error.h>
#include <fcntl.h>
#include <linux/filter.h>
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

#define FEAT_RX_MAP_CPU 0
#define FEAT_THREAD_MAP_CPU 1
#define FEAT_BPF_SELECT_CORE 1

namespace urn_mmsg {

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

  fprintf(stderr, "%s\n", strerror(code));
  abort();
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

int create_udp_socket(struct addrinfo* address_list) {
  // quickhack: only use the first address
  struct addrinfo* address = address_list;

  int socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  ensure_success(socket_fd);

  set_nonblocking(socket_fd);

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

void attach_bpf(int fd) {
  struct sock_filter code[] = {
      /* A = raw_smp_processor_id() */
      {BPF_LD | BPF_W | BPF_ABS, 0, 0, uint32_t(SKF_AD_OFF + SKF_AD_CPU)},
      /* return A */
      {BPF_RET | BPF_A, 0, 0, 0},
  };
  struct sock_fprog p;
  p.len = 2;
  p.filter = code;

  if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &p, sizeof(p)))
    error(1, errno, "failed to set SO_ATTACH_REUSEPORT_CBPF");
}

void set_socket_cpu_affinity(int socket_fd, int32_t cpu_id) {
  ensure_success(setsockopt(socket_fd, SOL_SOCKET, SO_INCOMING_CPU, &cpu_id, sizeof(cpu_id)));
}

void bind_socket(int fd, struct addrinfo* address) {
  int bind_status = bind(fd, address->ai_addr, address->ai_addrlen);

  if (bind_status == 0) {
    return;
  }

  printf("error binding socket: %s\n", strerror(bind_status));

  abort();
}

void print_address(const struct addrinfo* address) {
  char readable_ip[INET6_ADDRSTRLEN] = {0};

  if (address->ai_family == AF_INET) {
    struct sockaddr_in* ipv4 = (struct sockaddr_in*)address->ai_addr;
    inet_ntop(ipv4->sin_family, &ipv4->sin_addr, readable_ip, sizeof(readable_ip));
    printf("%s:%d\n", readable_ip, ntohs(ipv4->sin_port));
  } else if (address->ai_family == AF_INET6) {
    struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)address->ai_addr;
    inet_ntop(ipv6->sin6_family, &ipv6->sin6_addr, readable_ip, sizeof(readable_ip));
    printf("[%s]:%d\n", readable_ip, ntohs(ipv6->sin6_port));
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

} // namespace

const int32_t k_memory_per_packet = 1024;
const int32_t k_mmsg_capacity = 65536;

struct io_worker;

thread_local io_worker* local_io = nullptr;

struct io_vectors {
  int32_t capacity;
  int32_t packet_size;
  endpoint_t* addresses;
  struct iovec* vectors;
  struct mmsghdr* messages;
};

void io_vectors_init(io_vectors* vectors, uint8_t* mem, int32_t capacity, int32_t packet_size) {
  vectors->capacity = capacity;
  vectors->packet_size = packet_size;
  vectors->addresses = (endpoint_t*)calloc(capacity, sizeof(endpoint_t));
  vectors->vectors = (struct iovec*)calloc(capacity, sizeof(struct iovec));
  vectors->messages = (struct mmsghdr*)calloc(capacity, sizeof(struct mmsghdr));

  for (int32_t i = 0; i < capacity; i++) {
    int32_t mem_offset = packet_size * i;
    vectors->vectors[i].iov_base = mem + mem_offset;
    vectors->vectors[i].iov_len = packet_size;
    vectors->messages[i].msg_hdr.msg_name = &vectors->addresses[i];
    vectors->messages[i].msg_hdr.msg_namelen = sizeof(*vectors->addresses);
    vectors->messages[i].msg_hdr.msg_iov = &vectors->vectors[i];
    vectors->messages[i].msg_hdr.msg_iovlen = 1;
  }
}

struct io_state {
  int32_t tx_length;
  io_vectors rx;
  io_vectors tx;
};

void io_state_init(io_state* io, uint8_t* mem, int32_t capacity) {
  io->tx_length = 0;
  io_vectors_init(&io->rx, mem, capacity, k_memory_per_packet);
  io_vectors_init(&io->tx, mem, capacity, k_memory_per_packet);
}

struct io_worker_args {
  int32_t worker_index;
  urn_mmsg::relay* relay;
  struct addrinfo* local_client_address;
  struct addrinfo* local_peer_address;
  urn::countdown_latch* latch;
};

struct io_worker {
  urn_mmsg::relay* relay;
  int32_t peer_socket;
  int32_t client_socket;
  uint8_t* message_mem;
  io_state peers_io;
  io_state clients_io;
  int32_t worker_index;
};

void io_worker_init(io_worker* io, io_worker_args args) {
  io->relay = args.relay;
  io->peer_socket = create_udp_socket(args.local_peer_address);
  io->client_socket = create_udp_socket(args.local_client_address);

  io->message_mem = (uint8_t*)calloc(k_mmsg_capacity, k_memory_per_packet);

  io_state_init(&io->peers_io, io->message_mem, k_mmsg_capacity);
  io_state_init(&io->clients_io, io->message_mem, k_mmsg_capacity);

  io->worker_index = args.worker_index;
}

void io_worker_begin_frame(io_worker* io) { io->clients_io.tx_length = 0; }

void io_run_peer_receive_frame(io_worker* io) {
  int32_t messages_received =
      recvmmsg(io->peer_socket, io->peers_io.rx.messages, io->peers_io.rx.capacity, 0, nullptr);
  ensure_success(messages_received);

  for (int32_t i = 0; i < messages_received; i++) {
    const struct mmsghdr* mmsg_header = &io->peers_io.rx.messages[i];
    const struct msghdr* header = &mmsg_header->msg_hdr;
    const struct iovec* iov = &header->msg_iov[0];
    endpoint_t* from = (endpoint_t*)header->msg_name;
    mmsg::packet packet{(const std::byte*)iov->iov_base, mmsg_header->msg_len};
    io->relay->on_peer_received(*from, packet);
  }
}

void io_run_client_receive_frame(io_worker* io) {
  int32_t messages_received = recvmmsg(io->client_socket, io->clients_io.rx.messages,
                                       io->clients_io.rx.capacity, 0, nullptr);
  ensure_success(messages_received);

  for (int32_t i = 0; i < messages_received; i++) {
    const struct mmsghdr* mmsg_header = &io->clients_io.rx.messages[i];
    const struct msghdr* header = &mmsg_header->msg_hdr;
    const struct iovec* iov = &header->msg_iov[0];
    endpoint_t* from = (endpoint_t*)header->msg_name;
    io->relay->on_client_received(
        *from, mmsg::packet((const std::byte*)iov->iov_base, mmsg_header->msg_len));
  }
}

void io_run_client_send_frame(io_worker* io) {
  int32_t remaining = io->clients_io.tx_length;
  int32_t offset = 0;

  while (remaining > 0) {
    int32_t sent = sendmmsg(io->client_socket, &io->clients_io.tx.messages[offset], remaining, 0);

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

bool is_main_worker(int32_t thread_id) { return thread_id == 0; }

void worker(io_worker_args args) {
  io_worker state;
  io_worker_init(&state, args);
  local_io = &state;

  if (FEAT_RX_MAP_CPU) {
    set_socket_cpu_affinity(state.client_socket, args.worker_index);
    set_socket_cpu_affinity(state.peer_socket, args.worker_index);
  }

  if (FEAT_BPF_SELECT_CORE) {
    attach_bpf(state.client_socket);
    attach_bpf(state.peer_socket);
  }

  if (FEAT_THREAD_MAP_CPU) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(args.worker_index, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
      printf("thread affinity failed\n");
    }
  }

  args.latch->wait();

  bind_socket(state.client_socket, args.local_client_address);
  bind_socket(state.peer_socket, args.local_peer_address);

  int statistics_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
  ensure_success(statistics_timer_fd);

  if (is_main_worker(args.worker_index)) {
    struct timespec timespec;
    timespec.tv_sec = 5;
    timespec.tv_nsec = 0;
    struct itimerspec timer;
    timer.it_interval = timespec;
    timer.it_value = timespec;
    ensure_success(timerfd_settime(statistics_timer_fd, TFD_TIMER_ABSTIME, &timer, nullptr));
  }

  state.relay->on_thread_start(args.worker_index);

  for (;;) {
    io_worker_begin_frame(&state);

    const nfds_t num_fds = 3;
    struct pollfd poll_entries[num_fds];

    struct pollfd* peer_poll_entry = &poll_entries[0];
    struct pollfd* client_poll_entry = &poll_entries[1];
    struct pollfd* timer_poll_entry = &poll_entries[2];

    peer_poll_entry->fd = state.peer_socket;
    peer_poll_entry->events = POLLIN;
    peer_poll_entry->revents = 0;

    client_poll_entry->fd = state.client_socket;
    client_poll_entry->events = POLLIN;
    client_poll_entry->revents = 0;

    timer_poll_entry->fd = statistics_timer_fd;
    timer_poll_entry->events = POLLIN;
    timer_poll_entry->revents = 0;

    poll(poll_entries, num_fds, -1);

    if ((client_poll_entry->revents & POLLIN) != 0) {
      io_run_client_receive_frame(&state);
    }

    if ((peer_poll_entry->revents & POLLIN) != 0) {
      io_run_peer_receive_frame(&state);
    }

    if ((timer_poll_entry->revents & POLLIN) != 0) {
      uint64_t exp = 0;
      ensure_success(read(statistics_timer_fd, &exp, sizeof(exp)));
      state.relay->on_statistics_tick();
    }

    io_run_client_send_frame(&state);
  }
}

relay::relay(const urn_mmsg::config& conf) noexcept
    : config_{conf}, logic_{config_.threads, client_, peer_} {}

int relay::run() noexcept {
  int32_t thread_count = std::max(uint16_t(1), config_.threads);

  struct addrinfo* local_client_address = bindable_address("3478");
  struct addrinfo* local_peer_address = bindable_address("3479");
  print_address(local_client_address);
  print_address(local_peer_address);

  urn::countdown_latch latch(thread_count);

  auto create_worker_args = [=, &latch](int32_t worker_index) {
    io_worker_args args;
    args.worker_index = worker_index;
    args.relay = this;
    args.local_client_address = local_client_address;
    args.local_peer_address = local_peer_address;
    args.latch = &latch;
    return args;
  };

  std::vector<std::thread> worker_threads;

  for (int32_t i = 1; i < thread_count; i++) {
    worker_threads.emplace_back(worker, create_worker_args(i));
  }

  worker(create_worker_args(0));

  return 0;
}

void mmsg::session::start_send(const mmsg::packet& packet) noexcept {
  io_worker* io = local_io;

  int32_t index = io->clients_io.tx_length;

  io_vectors* tx = &io->clients_io.tx;
  tx->addresses[index] = client_endpoint;
  tx->vectors[index].iov_base = (void*)packet.data();
  tx->vectors[index].iov_len = packet.size();

  io->clients_io.tx_length++;

  io->relay->on_session_sent(*this, packet);
}

} // namespace urn_mmsg
