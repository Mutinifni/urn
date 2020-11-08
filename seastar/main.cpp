#include <seastar/core/app-template.hh>
#include <seastar/core/distributed.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <thread>
#include <urn/relay.hpp>

using namespace seastar;
using namespace net;
using namespace std::chrono_literals;

struct seastar_api {
  using endpoint = socket_address;
  struct packet;
  struct client;
  struct peer;
  struct session;
};

struct seastar_api::packet {
  packet(const char* data, size_t length) : content((const std::byte*)data), length(length) {}
  packet(const std::byte* data, size_t length) : content(data), length(length) {}

  const std::byte* data() const noexcept { return content; }

  size_t size() const noexcept { return length; }

  const std::byte* content;
  size_t length;
};

struct seastar_api::client {
  void start_receive() noexcept {}
};

struct seastar_api::peer {
  void start_receive() noexcept {}
};

struct seastar_api::session {
  const endpoint client_endpoint;

  session(const endpoint& client_endpoint) noexcept : client_endpoint(client_endpoint) {}

  void start_send(const seastar_api::packet& packet) noexcept { (void)packet; }
};

using relay_t = urn::relay<seastar_api, false>;

struct relay_worker {
  udp_channel client_chan = {};
  udp_channel peer_chan = {};
  relay_t* relay = nullptr;

  future<> start(relay_t* rel, uint16_t client_port, uint16_t peer_port) {
    printf("%u starting\n", this_shard_id());
    relay = rel;
    client_chan = make_udp_channel(ipv4_addr{client_port});
    peer_chan = make_udp_channel(ipv4_addr{peer_port});

    /*
    timer<> stats_timer;
    stats_timer.set_callback([this] { relay->print_statistics(5s); });
    stats_timer.arm_periodic(5s);
    */

    relay->on_thread_start(this_shard_id());
    auto client_loop = keep_doing([this] {
      return client_chan.receive().then([this](udp_datagram dgram) {
        auto& frag = dgram.get_data().frag(0);
        relay->on_client_received(dgram.get_src(), seastar_api::packet(frag.base, frag.size));
        return make_ready_future<>();
      });
    });

    auto peer_loop = keep_doing([this] {
      return peer_chan.receive().then([this](udp_datagram dgram) {
        auto pkt = std::move(dgram.get_data());
        auto& frag = pkt.frag(0);
        auto stat_packet = seastar_api::packet(frag.base, frag.size);
        auto session = relay->receive_peer_data(stat_packet);
        if (!session) {
          printf("session not found\n");
          return make_ready_future<>();
        }

        return client_chan.send(session->client_endpoint, std::move(pkt)).then([this, stat_packet] {
          relay->on_session_sent(stat_packet);
        });
      });
    });

    printf("%u begin loop\n", this_shard_id());
    return when_all(std::move(client_loop), std::move(peer_loop)).then([](auto t) {
      printf("%u exiting\n", this_shard_id());
      (void)t;
      return make_ready_future<>();
    });
  }

  future<> stop() {
    client_chan.close();
    peer_chan.close();
    return make_ready_future<>();
  }
};

int main(int argc, char** argv) {
  app_template app;

  seastar_api::client cl{};
  seastar_api::peer peer{};

  app.run(argc, argv, [&] {
    relay_t* relay = new relay_t(smp::count, cl, peer);
    std::cout << "smp: " << smp::count << "\n";
    auto server = new distributed<relay_worker>();

    return server->start()
        .then([server = std::move(server), relay]() mutable {
          engine().at_exit([server] { return server->stop(); });
          return server->invoke_on_all(&relay_worker::start, relay, 3478, 3479);
        })
        .then([] { std::cout << "Listening\n"; });
  });

  return 0;
}
