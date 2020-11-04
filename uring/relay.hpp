#pragma once

#include <chrono>
#include <cstdlib>
#include <netinet/in.h>
#include <urn/relay.hpp>

namespace urn_uring {

struct config {
  static constexpr std::chrono::seconds statistics_print_interval{5};

  struct {
    const char* port = "3478";
  } client{};

  struct {
    const char* port = "3479";
  } peer{};

  uint16_t threads;

  config(int argc, const char* argv[]);
};

struct uring {
  struct endpoint;
  struct packet;
  struct client;
  struct peer;
  struct session;
};

struct uring::endpoint {
  endpoint() = default;
  endpoint(sockaddr_storage address, void* user_data)
      : address(address), user_data(user_data) {
  }
  sockaddr_storage address = {};
  void* user_data = nullptr;
};

struct uring::packet {
  packet(const std::byte* content, size_t length) : content(content), length(length) {}

  const std::byte* data() const noexcept { return content; }

  size_t size() const noexcept { return length; }

  const std::byte* content;
  size_t length;
};

struct uring::client {
  void start_receive() noexcept {}
};

struct uring::peer {
  void start_receive() noexcept {}
};

struct uring::session {
  const endpoint client_endpoint;

  session(const endpoint& client_endpoint) noexcept : client_endpoint(client_endpoint) {}

  void start_send(const uring::packet& packet) noexcept;
};

class relay {
public:
  relay(const urn_uring::config& conf) noexcept;

  int run() noexcept;

  const urn_uring::config& config() const noexcept { return config_; }

  void on_thread_start(uint16_t thread_index) { logic_.on_thread_start(thread_index); }

  void on_client_received(const uring::endpoint& src, const uring::packet& packet) {
    logic_.on_client_received(src, packet);
  }

  bool on_peer_received(const uring::endpoint& src, uring::packet& packet) {
    return logic_.on_peer_received(src, packet);
  }

  void on_session_sent(uring::session& session, const uring::packet& packet) {
    logic_.on_session_sent(session, packet);
  }

  void on_statistics_tick() noexcept { logic_.print_statistics(config_.statistics_print_interval); }

private:
  uring::client client_{};
  uring::peer peer_{};

  const urn_uring::config config_;
  urn::relay<uring, false> logic_;
};

} // namespace urn_uring
