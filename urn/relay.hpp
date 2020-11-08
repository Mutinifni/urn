#pragma once

/**
 * \file urn/relay.hpp
 * UDP relay business logic
 *
 * See README.md for required Library API
 */

#include <urn/__bits/lib.hpp>
#include <urn/mutex.hpp>
#include <iomanip>
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
      session_id id = get_session_id(packet.data());
      // printf("client received sess %lu\n", id);
      if (try_register_session(id, src))
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
      session_id id = get_session_id(packet.data());
      if (auto session = find_session(id))
      {
        // peer receive is restarted when sending finishes
        // (on_session_sent is invoked)
        session->start_send(packet);
        return true;
      }
      // printf("session %lu not found\n", id);
    }
    peer_.start_receive();
    return false;
  }

  template <typename WithSession>
  bool on_peer_received (const packet_type& packet, WithSession with_session)
  {
    update_io_statistics(this_thread_statistics_->in, packet);
    if (packet.size() >= sizeof(session_id))
    {
      session_id id = get_session_id(packet.data());
      if (auto session = find_session(id))
      {
        with_session(session);
        return true;
      }
      // printf("session %lu not found\n", id);
    }
    peer_.start_receive();
    return false;
  }


  void on_session_sent (session_type &, const packet_type &packet)
  {
    update_io_statistics(this_thread_statistics_->out, packet);
    peer_.start_receive();
  }

  void on_session_sent (const packet_type &packet)
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

  std::optional<session_type> receive_peer_data(const packet_type& packet) {
    update_io_statistics(this_thread_statistics_->in, packet);
    if (packet.size() >= sizeof(session_id))
    {
      session_id id = get_session_id(packet.data());
      if (auto session = find_session(id))
      {
        return *session;
      }
    }
  
    return std::nullopt;
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

    void get_and_reset_into (statistics &dest)
    {
      dest.in.packets = std::exchange(in.packets, 0);
      dest.in.bytes = std::exchange(in.bytes, 0);
      dest.out.packets = std::exchange(out.packets, 0);
      dest.out.bytes = std::exchange(out.bytes, 0);
    }

    void sum_into (statistics &dest)
    {
      dest.in.packets += in.packets;
      dest.in.bytes += in.bytes;
      dest.out.packets += out.packets;
      dest.out.bytes += out.bytes;
    }
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


  statistics load_statistics (std::string &in_bytes_distribution) noexcept
  {
    // not thread-safe but good enough to skip sync overhead

    // aggregate and reset per thread stats
    statistics total{};
    std::vector<statistics> per_thread_statistics(per_thread_statistics_.size());
    for (size_t i = 0;  i != per_thread_statistics_.size();  ++i)
    {
      per_thread_statistics_[i].get_and_reset_into(per_thread_statistics[i]);
      per_thread_statistics[i].sum_into(total);
    }

    // calculate ingress distribution between threads (result as string)
    in_bytes_distribution.clear();
    for (auto &statistics: per_thread_statistics)
    {
      std::ostringstream oss;
      oss
        << std::fixed
        << std::setprecision(0)
        << (total.in.bytes ? statistics.in.bytes * 100.0 / total.in.bytes : 0.0)
        << "%/"
      ;
      in_bytes_distribution += oss.str();
    }
    if (in_bytes_distribution.size())
    {
      in_bytes_distribution.pop_back();
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
