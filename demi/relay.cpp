#include <arpa/inet.h>
#include <cstring>
#include <demi/relay.hpp>
#include <deque>
#include <dmtr/libos.h>
#include <dmtr/wait.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

uint64_t num_packets = 0;
uint64_t recv_packets = 0;
uint64_t start_time = 0;
uint64_t tstart = 0;
uint64_t tnext = 0;


namespace urn_demi {

config::config(int argc, const char* argv[])
    : threads{1} {
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

void dmtr_ok(int res) {
  if (res == 0) {
    return;
  }

  fprintf(stderr, "dmtr error code %d", res);
  abort();
}

int create_udp_socket(struct addrinfo* address_list) {
  struct addrinfo* address = address_list;

  int sfd;
  dmtr_ok(dmtr_socket(&sfd, AF_INET, SOCK_DGRAM, 0));
  dmtr_ok(dmtr_bind(sfd, address->ai_addr, address->ai_addrlen));

  return sfd;
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
const int32_t k_demi_capacity = 65536;

struct io_worker;

thread_local io_worker* local_io = nullptr;

struct io_vectors {
  int32_t capacity;
  int32_t packet_size;
  dmtr_qresult_t res; // filled each loop
  dmtr_qtoken_t op_token;
};

void io_vectors_init(io_vectors* vectors, uint8_t* mem, int32_t capacity, int32_t packet_size) {
  (void)mem;
  vectors->capacity = capacity;
  vectors->packet_size = packet_size;
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
  urn_demi::relay* relay;
  struct addrinfo* local_client_address;
  struct addrinfo* local_peer_address;
};

struct io_worker {
  urn_demi::relay* relay;
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

  io->message_mem = (uint8_t*)calloc(k_demi_capacity, k_memory_per_packet);

  io_state_init(&io->peers_io, io->message_mem, k_demi_capacity);
  io_state_init(&io->clients_io, io->message_mem, k_demi_capacity);

  io->worker_index = args.worker_index;
}

int io_worker_begin_frame(io_worker* io) {
  io->clients_io.tx_length = 0;

  dmtr_ok(dmtr_pop(&io->clients_io.rx.op_token, io->client_socket));
  dmtr_ok(dmtr_pop(&io->peers_io.rx.op_token, io->peer_socket));

  return 0;
}

bool is_main_worker(int32_t thread_id) { return thread_id == 0; }


int worker(io_worker_args args) {
  io_worker state;
  io_worker_init(&state, args);

  
  printf("state init done\n");
  local_io = &state;

  state.relay->on_thread_start(args.worker_index);

  for (;;) {
    io_worker_begin_frame(&state);

    dmtr_qresult_t result;

    dmtr_qtoken_t tokens[2] = {
        state.peers_io.rx.op_token,
        state.clients_io.rx.op_token,
    };

    int token_index = -1;
    //printf("wait any\n");
    dmtr_ok(dmtr_wait_any(&result, &token_index, tokens, 2));

    result.qr_value.sga.sga_addr.sin_port = htons(result.qr_value.sga.sga_addr.sin_port);
    struct sockaddr_in from = result.qr_value.sga.sga_addr;
    //printf("port: %u\n", result.qr_value.sga.sga_addr.sin_port);
    //printf("ntohs port: %u\n", ntohs(result.qr_value.sga.sga_addr.sin_port));
    dmtr_sgaseg_t sga = result.qr_value.sga.sga_segs[0];

    if (token_index == 0) {
      //printf("received peer data\n");
      state.peers_io.rx.res = result;
      demi::packet packet{(const std::byte*)sga.sgaseg_buf, sga.sgaseg_len};
      printf("peer data sgaseg_buf: %s\n", (char *) sga.sgaseg_buf);
      fflush(stdout);
      //printf("peer data sgaseg_len: %u\n", sga.sgaseg_len);
      recv_packets++;

      dmtr_qtoken_t qt;
      dmtr_ok(dmtr_pushto(&qt, state.client_socket, &result.qr_value.sga,
                           (const sockaddr*)&state.clients_io.rx.res.qr_value.sga.sga_addr,
                           sizeof(struct sockaddr_in)));

      //dmtr_sgafree(&io->peers_io.rx.res.qr_value.sga);

      dmtr_qresult_t qr;
      dmtr_ok(dmtr_wait(&qr, qt) != 0);
 
      state.relay->on_peer_received(from, packet);

	} else if (token_index == 1) {
      //printf("received client data\n");
      state.clients_io.rx.res = result;
      state.relay->on_client_received(
          from, demi::packet((const std::byte*)sga.sgaseg_buf, sga.sgaseg_len));

    } else {
      printf("wait fail index %d\n", token_index);
    }

    // io_run_client_send_frame(&state);
	}

  return 0;
}

relay::relay(const urn_demi::config& conf) noexcept
    : config_{conf}, logic_{config_.threads, client_, peer_} {}

int relay::run() noexcept {
  struct addrinfo* local_client_address = bindable_address("3478");
  struct addrinfo* local_peer_address = bindable_address("3479");
  
  auto create_worker_args = [=](int32_t worker_index) {
    io_worker_args args;
    args.worker_index = worker_index;
    args.relay = this;
    args.local_client_address = local_client_address;
    args.local_peer_address = local_peer_address;
    return args;
  };

  worker(create_worker_args(0));

  return 0;
}

void demi::session::start_send(const demi::packet& packet) noexcept {
  io_worker* io = local_io;
  io->clients_io.tx_length++;

  // print packet info
  //printf("packet size: %zu\n", packet.size());
  //printf("packet data: %s\n", (char *) packet.data());

  // print sga info
  //printf("sga: %s\n", (char *) io->peers_io.rx.res.qr_value.sga.sga_segs[0].sgaseg_buf);
  //printf("sgalen: %u\n", io->peers_io.rx.res.qr_value.sga.sga_segs[0].sgaseg_len);
  //printf("sga addr: %p\n", (void *) &io->peers_io.rx.res.qr_value.sga);
  //printf("num segs: %d\n", io->peers_io.rx.res.qr_value.sga.sga_numsegs);

  //dmtr_qtoken_t qt;
  //dmtr_ok(dmtr_pushto(&qt, io->client_socket, &io->peers_io.rx.res.qr_value.sga,
  //                     (const sockaddr*)&io->clients_io.rx.res.qr_value.sga.sga_addr,
  //                     sizeof(struct sockaddr_in)));

  //dmtr_sgafree(&io->peers_io.rx.res.qr_value.sga);

  //dmtr_qresult_t qr;
  //dmtr_ok(dmtr_wait(&qr, qt) != 0);

  // print qr value
  //printf("qr addr: %p\n", (void *) &qr.qr_value.sga);
  //printf("qr data: %s\n", (char *) qr.qr_value.sga.sga_segs[0].sgaseg_buf);
  //dmtr_sgafree(&qr.qr_value.sga);

  num_packets++;
  if (num_packets % 100000 == 0) {
      if (start_time == 0) {
          start_time = rdtscp(NULL);
      }
      tnext = rdtscp(NULL);
      printf("out: %f\n", (100000 * 1024 * 8 * 2.5) / ((tnext - tstart)));
      tstart = tnext;
  }

  //printf("finish waiting\n");

  io->relay->on_session_sent(*this, packet);
}

} // namespace urn_demi
