#pragma once

/**
 * \file urn/relay.hpp
 * UDP relay business logic
 */

#include <urn/__bits/lib.hpp>
#include <urn/mutex.hpp>
#include <unordered_map>


__urn_begin


#if 0
struct Library
{
  // Source/destination endpoint
  using endpoint = /**/

  // Time point
  using time = /**/


  struct packet
  {
    std::byte *data ();
    size_t size ();
  };


  struct client
  {
    // Start receive on client port
    // On completion, invoke relay<Library>::on_client_received()
    void start_receive ();
  };


  struct peer
  {
    // Start receive on peer port
    // On completion, invoke relay<Library>::on_peer_received()
    void start_receive ();
  };


  struct session
  {
    // Construct new session with associated endpoint \a src
    session (const endpoint &src);

    // Start sending \a data to associated endpoint
    // On completion, invoke relay<Library>::on_session_sent()
    void start_send (packet &&p);

    // Return true if session is invalidated
    bool is_invalidated (const time &now) const;
  };
};
#endif


template <typename Library, bool MultiThreaded = false>
class relay
{
public:

  using endpoint_type = typename Library::endpoint;
  using time_type = typename Library::time;
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


  void tick (const time_type &now)
  {
    erase_invalidated_sessions(now);
  }


  void on_client_received (const endpoint_type &src, const packet_type &packet)
  {
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


  void on_session_sent (session_type &, const packet_type &)
  {
    peer_.start_receive();
  }


  session_type *find_session (session_id id)
  {
    std::lock_guard lock{sessions_mutex_};
    if (auto it = sessions_.find(id); it != sessions_.end())
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


  void erase_invalidated_sessions (const time_type &now)
  {
    std::lock_guard lock{sessions_mutex_};
    for (auto it = sessions_.begin(), end = sessions_.end();  it != end;  )
    {
      if (it->second.is_invalidated(now))
      {
        it = sessions_.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }
};


__urn_end