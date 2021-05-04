#pragma once

#include <chrono>
#include <cstdlib>
#include <netinet/in.h>
#include <urn/relay.hpp>

extern uint64_t num_packets;
extern uint64_t recv_packets;
extern uint64_t start_time;
extern uint64_t tstart;
extern uint64_t tnext;

static inline uint64_t rdtscp(uint32_t *auxp)
{
    uint32_t a, d, c;
    asm volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
    if (auxp)
        *auxp = c;
    return ((uint64_t)a) | (((uint64_t)d) << 32);
}

namespace urn_demi {

struct config {
  static constexpr std::chrono::seconds statistics_print_interval{1};

  struct {
    const char* port = "3478";
  } client{};

  struct {
    const char* port = "3479";
  } peer{};

  uint16_t threads;

  config(int argc, const char* argv[]);
};

using endpoint_t = struct sockaddr_in;

struct demi {
  using endpoint = endpoint_t;
  struct packet;
  struct client;
  struct peer;
  struct session;
};

struct demi::packet {
  packet(const std::byte* content, size_t length) : content(content), length(length) {}

  const std::byte* data() const noexcept { return content; }

  size_t size() const noexcept { return length; }

  const std::byte* content;
  size_t length;
};

struct demi::client {
  void start_receive() noexcept {}
};

struct demi::peer {
  void start_receive() noexcept {}
};

struct demi::session {
  const endpoint client_endpoint;

  session(const endpoint& client_endpoint) noexcept : client_endpoint(client_endpoint) {}

  void start_send(const demi::packet& packet) noexcept;
};

class relay {
public:
  relay(const urn_demi::config& conf) noexcept;

  int run() noexcept;

  const urn_demi::config& config() const noexcept { return config_; }

  void on_thread_start(uint16_t thread_index) { logic_.on_thread_start(thread_index); }

  void on_client_received(const demi::endpoint& src, const demi::packet& packet) {
    logic_.on_client_received(src, packet);
  }

  bool on_peer_received(const demi::endpoint& src, demi::packet& packet) {
    return logic_.on_peer_received(src, packet);
  }

  void on_session_sent(demi::session& session, const demi::packet& packet) {
    logic_.on_session_sent(session, packet);
  }

  void on_statistics_tick() noexcept { logic_.print_statistics(config_.statistics_print_interval); }

private:
  demi::client client_{};
  demi::peer peer_{};

  const urn_demi::config config_;
  urn::relay<demi, false> logic_;
};

} // namespace urn_demi
