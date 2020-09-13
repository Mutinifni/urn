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
#include <sstream>
#include <vector>


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

  using mutex_type = shared_mutex<MultiThreaded>;


  relay (uint16_t thread_count, client_type &client, peer_type &peer) noexcept
    : client_{client}
    , peer_{peer}
    , per_thread_statistics_{thread_count}
  { }


  void print_statistics (const std::chrono::seconds &interval)
  {
    std::string bytes_in_distribution;
    auto stats = load_statistics(bytes_in_distribution);
    auto [in_bps, in_unit] = bits_per_sec(stats.in.bytes, interval);
    auto [out_bps, out_unit] = bits_per_sec(stats.out.bytes, interval);
    stats.in.packets /= interval.count();
    stats.out.packets /= interval.count();
    std::cout
      << "in: " << stats.in.packets << '/' << in_bps << in_unit
      << " | out: " << stats.out.packets << '/' << out_bps << out_unit
      << " | dist " << bytes_in_distribution
      << '\n';
  }


  void on_thread_start (uint16_t thread_index)
  {
    this_thread_statistics_ = &per_thread_statistics_.at(thread_index);
  }


  void on_client_received (const endpoint_type &src, const packet_type &packet)
  {
    update_io_statistics(this_thread_statistics_->in, packet);
    if (packet.size() == sizeof(session_id))
    {
      if (try_register_session(get_session_id(packet.data()), src))
      {
        peer_.start_receive();
      }
    }
    client_.start_receive();
  }


  bool on_peer_received (const endpoint_type &, const packet_type &packet)
  {
    update_io_statistics(this_thread_statistics_->in, packet);
    if (packet.size() >= sizeof(session_id))
    {
      if (auto session = find_session(get_session_id(packet.data())))
      {
        // peer receive is restarted when sending finishes
        // (on_session_sent is invoked)
        session->start_send(packet);
        return true;
      }
    }
    peer_.start_receive();
    return false;
  }


  void on_session_sent (session_type &, const packet_type &packet)
  {
    update_io_statistics(this_thread_statistics_->out, packet);
    peer_.start_receive();
  }


  session_type *find_session (session_id id)
  {
    std::shared_lock lock{sessions_mutex_};
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

  struct statistics
  {
    struct direction
    {
      size_t packets, bytes;
    } in{}, out{};
  };
  std::vector<statistics> per_thread_statistics_;
  static inline thread_local statistics *this_thread_statistics_{};


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


  void update_io_statistics (typename statistics::direction &dir,
    const packet_type &packet) noexcept
  {
    dir.bytes += packet.size();
    dir.packets++;
  }


  statistics load_statistics (std::string &bytes_in_distribution) noexcept
  {
    std::vector<size_t> bytes_in_per_thread(per_thread_statistics_.size());
    auto it = bytes_in_per_thread.begin();

    // not thread safe but good enough for statistics purpose
    statistics total{};
    for (auto &stats: per_thread_statistics_)
    {
      auto in_bytes = std::exchange(stats.in.bytes, 0);
      *it++ = in_bytes;

      total.in.bytes += in_bytes;
      total.in.packets += std::exchange(stats.in.packets, 0);

      total.out.bytes += std::exchange(stats.out.bytes, 0);
      total.out.packets += std::exchange(stats.out.packets, 0);
    }

    bytes_in_distribution.clear();
    for (auto bytes_in: bytes_in_per_thread)
    {
      std::ostringstream oss;
      oss << (total.in.bytes ? bytes_in * 100 / total.in.bytes : 0) << "%/";
      bytes_in_distribution += oss.str();
    }
    if (!bytes_in_distribution.empty())
    {
      bytes_in_distribution.pop_back();
    }

    return total;
  }


  static constexpr std::pair<size_t, const char *> bits_per_sec (
    size_t bytes, const std::chrono::seconds &interval) noexcept
  {
    constexpr const char *units[] = { "bps", "Kbps", "Mbps", "Gbps", };
    auto unit = std::cbegin(units);

    auto bits_per_sec = 8 * bytes / interval.count();
    while (bits_per_sec > 1000 && (unit + 1) != std::cend(units))
    {
      bits_per_sec /= 1000;
      unit++;
    }

    return { bits_per_sec, *unit };
  }
};


__urn_end
