#pragma once

/**
 * \file urn/relay.hpp
 * UDP relay business logic
 *
 * See README.md for required Library API
 */

#include <urn/__bits/lib.hpp>
#include <urn/mutex.hpp>
#include <iostream>
#include <unordered_map>


__urn_begin


template <typename Library, bool MultiThreaded = false>
class relay
{
public:

  using endpoint_type = typename Library::endpoint;
  using packet_type = typename Library::packet;

  using client_type = typename Library::client;
  using peer_type = typename Library::peer;

  using session_id = uint64_t;
  using session_type = typename Library::session;

  using mutex_type = mutex<MultiThreaded>;


  relay (client_type &client, peer_type &peer) noexcept
    : client_{client}
    , peer_{peer}
  { }


  void print_statistics (const std::chrono::seconds &interval)
  {
    auto [in_packets, in_bytes, out_packets, out_bytes] = load_io_statistics();
    auto [in_bps, in_unit] = bits_per_sec(in_bytes, interval);
    auto [out_bps, out_unit] = bits_per_sec(out_bytes, interval);
    std::cout
      << "in: " << in_packets << '/' << in_bps << in_unit
      << " | "
      << "out: " << out_packets << '/' << out_bps << out_unit
      << '\n';
  }


  void on_client_received (const endpoint_type &src, const packet_type &packet)
  {
    update_io_statistics(stats_.in.packets, stats_.in.bytes, packet.size());
    if (packet.size() == sizeof(session_id))
    {
      if (try_register_session(get_session_id(packet.data()), src))
      {
        peer_.start_receive();
      }
    }
    client_.start_receive();
  }


  bool on_peer_received (const endpoint_type &, packet_type &&packet)
  {
    update_io_statistics(stats_.in.packets, stats_.in.bytes, packet.size());
    if (packet.size() >= sizeof(session_id))
    {
      if (auto session = find_session(get_session_id(packet.data())))
      {
        // peer receive is restarted when sending finishes
        // (on_session_sent is invoked)
        session->start_send(std::move(packet));
        return true;
      }
    }
    peer_.start_receive();
    return false;
  }


  void on_session_sent (session_type &, const packet_type &packet)
  {
    update_io_statistics(stats_.out.packets, stats_.out.bytes, packet.size());
    peer_.start_receive();
  }


  session_type *find_session (session_id id)
  {
    std::lock_guard lock{sessions_mutex_};
    if (auto it = sessions_.find(id);  it != sessions_.end())
    {
      return &it->second;
    }
    return nullptr;
  }


private:

  client_type &client_;
  peer_type &peer_;

  using session_map = std::unordered_map<session_id, session_type>;
  session_map sessions_{};
  mutable mutex_type sessions_mutex_{};

  struct
  {
    struct
    {
      size_t packets, bytes;
    } in{}, out{};
  } stats_{};
  mutable mutex_type stats_mutex_{};


  static session_id get_session_id (const std::byte *data)
  {
    // htonll not used:
    //  - tested libs/OS are all little-endian (for now)
    //  - simplifies test code
    return *reinterpret_cast<const session_id *>(data);
  }


  bool try_register_session (session_id id, const endpoint_type &src)
  {
    std::lock_guard lock{sessions_mutex_};
    return sessions_.try_emplace(id, src).second;
  }


  void update_io_statistics (size_t &count_var, size_t &size_var, size_t bytes)
    noexcept
  {
    std::lock_guard lock{stats_mutex_};
    size_var += bytes;
    count_var++;
  }


  std::tuple<size_t, size_t, size_t, size_t> load_io_statistics () noexcept
  {
    std::lock_guard lock{stats_mutex_};
    return
    {
      std::exchange(stats_.in.packets, 0),
      std::exchange(stats_.in.bytes, 0),
      std::exchange(stats_.out.packets, 0),
      std::exchange(stats_.out.bytes, 0)
    };
  }


  static constexpr std::pair<size_t, const char *> bits_per_sec (
    size_t bytes, const std::chrono::seconds &interval) noexcept
  {
    constexpr const char *units[] = { "bps", "Kbps", "Mbps", "Gbps", };
    auto unit = std::cbegin(units);

    auto bits_per_sec = 8 * (bytes / interval.count());
    while (bits_per_sec > 1000 && (unit + 1) != std::cend(units))
    {
      bits_per_sec /= 1000;
      unit++;
    }

    return { bits_per_sec, *unit };
  }
};


__urn_end
