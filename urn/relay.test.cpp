#include <urn/relay.hpp>
#include <urn/common.test.hpp>
#include <utility>


namespace {


struct test_lib
{
  using endpoint = uint64_t;
  using time = int;


  struct packet
  {
    const std::byte *ptr;
    size_t len;


    template <typename C>
    packet (const C &c) noexcept
      : ptr{reinterpret_cast<const std::byte *>(std::data(c))}
      , len{std::size(c) * sizeof(*std::data(c))}
    { }


    const std::byte *data () const noexcept
    {
      return ptr;
    }


    size_t size () const noexcept
    {
      return len;
    }
  };


  struct client
  {
    bool start_recv_invoked = false;


    void start_receive ()
    {
      start_recv_invoked = true;
    }


    bool is_start_recv_invoked ()
    {
      return std::exchange(start_recv_invoked, false);
    }
  };


  struct peer
  {
    bool start_recv_invoked = false;


    void start_receive () noexcept
    {
      start_recv_invoked = true;
    }


    bool is_start_recv_invoked ()
    {
      return std::exchange(start_recv_invoked, false);
    }
  };


  struct session
  {
    inline static session *last_ = nullptr;

    const endpoint src;
    bool start_send_invoked = false;
    bool invalidated = false;


    session (const endpoint &src) noexcept
      : src{src}
    {
      last_ = this;
    }


    void start_send (packet &&) noexcept
    {
      start_send_invoked = true;
    }


    bool is_start_send_invoked ()
    {
      return std::exchange(start_send_invoked, false);
    }


    void invalidate () noexcept
    {
      invalidated = true;
    }


    bool is_invalidated (const time &) const noexcept
    {
      return invalidated;
    }


    static session *last_created () noexcept
    {
      return std::exchange(last_, nullptr);
    }
  };
};


using single_threaded = urn::relay<test_lib, false>;
using multi_threaded = urn::relay<test_lib, true>;


TEMPLATE_TEST_CASE("relay", "",
  single_threaded,
  multi_threaded)
{
  typename TestType::client_type client{};
  typename TestType::peer_type peer{};
  TestType relay{client, peer};

  constexpr uint64_t a_id = 1, b_id = 2;
  constexpr test_lib::endpoint a_src = 11, b_src = 22;


  SECTION("on_client_received: invalid registration")
  {
    uint64_t data[] = { a_id, 100 };
    relay.on_client_received(a_src, data);
    CHECK(test_lib::session::last_created() == nullptr);
    CHECK(client.is_start_recv_invoked());
    CHECK_FALSE(peer.is_start_recv_invoked());
  }


  SECTION("on_client_received: successful registration")
  {
    uint64_t data[] = { a_id };
    relay.on_client_received(a_src, data);

    // receives must be restarted
    CHECK(client.is_start_recv_invoked());
    CHECK(peer.is_start_recv_invoked());

    // session must be created
    auto a = test_lib::session::last_created();
    REQUIRE(a != nullptr);
    CHECK(a->src == a_src);
  }


  SECTION("on_client_received: duplicate registration")
  {
    uint64_t data[] = { a_id };

    // first
    relay.on_client_received(a_src, data);
    REQUIRE(test_lib::session::last_created() != nullptr);

    // second
    relay.on_client_received(b_src, data);
    CHECK(test_lib::session::last_created() == nullptr);
  }


  SECTION("on_client_received: multiple registrations")
  {
    // first
    {
      uint64_t data[] = { a_id };
      relay.on_client_received(a_src, data);
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      auto a = test_lib::session::last_created();
      REQUIRE(a != nullptr);
      CHECK(a->src == a_src);
    }

    // second
    {
      uint64_t data[] = { b_id };
      relay.on_client_received(b_src, data);
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      auto b = test_lib::session::last_created();
      REQUIRE(b != nullptr);
      CHECK(b->src == b_src);
    }
  }


  SECTION("on_peer_received: invalid data")
  {
    char data[] = { 'a' };
    CHECK_FALSE(relay.on_peer_received(a_src, data));
    CHECK_FALSE(client.is_start_recv_invoked());
    CHECK(peer.is_start_recv_invoked());
  }


  SECTION("on_peer_received: no registration")
  {
    // successful registration
    test_lib::session *session = nullptr;
    {
      uint64_t data[] = { a_id };
      relay.on_client_received(a_src, data);
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      session = test_lib::session::last_created();
      REQUIRE(session != nullptr);
    }

    // send to invalid registration
    {
      uint64_t data[] = { b_id, 100 };
      CHECK_FALSE(relay.on_peer_received(a_src, data));
      CHECK(peer.is_start_recv_invoked());
      CHECK_FALSE(session->is_start_send_invoked());
    }
  }


  SECTION("on_peer_received: successful forwarding")
  {
    // registration
    test_lib::session *session = nullptr;
    {
      uint64_t data[] = { a_id };
      relay.on_client_received(a_src, data);
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      session = test_lib::session::last_created();
      REQUIRE(session != nullptr);
    }

    // forward
    {
      uint64_t data[] = { a_id, 100 };
      CHECK(relay.on_peer_received(a_src, data));
      CHECK(session->is_start_send_invoked());

      // new receive is started only after session start_send has finished
      CHECK_FALSE(peer.is_start_recv_invoked());
      relay.on_session_sent(*session, data);
      CHECK(peer.is_start_recv_invoked());
    }
  }


  SECTION("on_peer_received: no cross-forwarding")
  {
    // register a
    test_lib::session *a = nullptr;
    {
      uint64_t data[] = { a_id };
      relay.on_client_received(a_src, data);
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      a = test_lib::session::last_created();
      REQUIRE(a != nullptr);
    }

    // register b
    test_lib::session *b = nullptr;
    {
      uint64_t data[] = { b_id };
      relay.on_client_received(b_src, data);
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      b = test_lib::session::last_created();
      REQUIRE(b != nullptr);
    }

    // forward
    {
      uint64_t data[] = { a_id, 100 };
      CHECK(relay.on_peer_received(a_src, data));
      CHECK(a->is_start_send_invoked());
      CHECK_FALSE(b->is_start_send_invoked());

      relay.on_session_sent(*a, data);
      CHECK(peer.is_start_recv_invoked());
    }
  }


  SECTION("tick: erase invalidated sessions")
  {
    // register a
    test_lib::session *a = nullptr;
    {
      uint64_t data[] = { a_id };
      relay.on_client_received(a_src, data);
      a = test_lib::session::last_created();
      REQUIRE(a != nullptr);
    }

    // register b
    test_lib::session *b = nullptr;
    {
      uint64_t data[] = { b_id };
      relay.on_client_received(b_src, data);
      b = test_lib::session::last_created();
      REQUIRE(b != nullptr);
    }

    CHECK(relay.find_session(a_id) == a);
    CHECK(relay.find_session(b_id) == b);

    relay.tick(0);
    CHECK(relay.find_session(a_id) == a);
    CHECK(relay.find_session(b_id) == b);

    b->invalidate();
    relay.tick(1);
    CHECK(relay.find_session(a_id) == a);
    CHECK(relay.find_session(b_id) == nullptr);

    a->invalidate();
    relay.tick(2);
    CHECK(relay.find_session(a_id) == nullptr);
    CHECK(relay.find_session(b_id) == nullptr);
  }
}


} // namespace
