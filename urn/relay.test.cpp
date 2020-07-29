#include <urn/relay.hpp>
#include <urn/common.test.hpp>
#include <array>
#include <utility>


namespace {


struct test_lib
{
  using endpoint = uint64_t;
  using time = int;


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


    void start_send (std::pair<const std::byte *, size_t>) noexcept
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


template <typename C>
std::pair<const std::byte *, size_t> as_bytes (const C &c)
{
  auto data = std::data(c);
  auto length = sizeof(*data) * std::size(c);
  return {reinterpret_cast<const std::byte *>(data), length};
}


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
    std::array<uint64_t, 2> packet = { a_id, 100 };
    relay.on_client_received(a_src, as_bytes(packet));
    CHECK(test_lib::session::last_created() == nullptr);
    CHECK(client.is_start_recv_invoked());
    CHECK_FALSE(peer.is_start_recv_invoked());
  }


  SECTION("on_client_received: successful registration")
  {
    std::array packet = { a_id };
    relay.on_client_received(a_src, as_bytes(packet));

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
    std::array packet = { a_id };

    // first
    relay.on_client_received(a_src, as_bytes(packet));
    REQUIRE(test_lib::session::last_created() != nullptr);

    // second
    relay.on_client_received(b_src, as_bytes(packet));
    CHECK(test_lib::session::last_created() == nullptr);
  }


  SECTION("on_client_received: multiple registrations")
  {
    // first
    {
      std::array packet = { a_id };
      relay.on_client_received(a_src, as_bytes(packet));
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      auto a = test_lib::session::last_created();
      REQUIRE(a != nullptr);
      CHECK(a->src == a_src);
    }

    // second
    {
      std::array packet = { b_id };
      relay.on_client_received(b_src, as_bytes(packet));
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      auto b = test_lib::session::last_created();
      REQUIRE(b != nullptr);
      CHECK(b->src == b_src);
    }
  }


  SECTION("on_peer_received: invalid data")
  {
    std::array packet = { char('a') };
    relay.on_peer_received(a_src, as_bytes(packet));
    CHECK_FALSE(client.is_start_recv_invoked());
    CHECK(peer.is_start_recv_invoked());
  }


  SECTION("on_peer_received: no registration")
  {
    // successful registration
    test_lib::session *session = nullptr;
    {
      std::array packet = { a_id };
      relay.on_client_received(a_src, as_bytes(packet));
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      session = test_lib::session::last_created();
      REQUIRE(session != nullptr);
    }

    // send to invalid registration
    {
      std::array<uint64_t, 2> packet = { b_id, 100 };
      relay.on_peer_received(a_src, as_bytes(packet));
      CHECK(peer.is_start_recv_invoked());
      CHECK_FALSE(session->is_start_send_invoked());
    }
  }


  SECTION("on_peer_received: successful forwarding")
  {
    // registration
    test_lib::session *session = nullptr;
    {
      std::array packet = { a_id };
      relay.on_client_received(a_src, as_bytes(packet));
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      session = test_lib::session::last_created();
      REQUIRE(session != nullptr);
    }

    // forward
    {
      std::array<uint64_t, 2> packet = { a_id, 100 };
      relay.on_peer_received(a_src, as_bytes(packet));
      CHECK(session->is_start_send_invoked());

      // new receive is started only after session start_send has finished
      CHECK_FALSE(peer.is_start_recv_invoked());
      relay.on_session_sent();
      CHECK(peer.is_start_recv_invoked());
    }
  }


  SECTION("on_peer_received: no cross-forwarding")
  {
    // register a
    test_lib::session *a = nullptr;
    {
      std::array packet = { a_id };
      relay.on_client_received(a_src, as_bytes(packet));
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      a = test_lib::session::last_created();
      REQUIRE(a != nullptr);
    }

    // register b
    test_lib::session *b = nullptr;
    {
      std::array packet = { b_id };
      relay.on_client_received(b_src, as_bytes(packet));
      CHECK(client.is_start_recv_invoked());
      CHECK(peer.is_start_recv_invoked());
      b = test_lib::session::last_created();
      REQUIRE(b != nullptr);
    }

    // forward
    {
      std::array<uint64_t, 2> packet = { a_id, 100 };
      relay.on_peer_received(a_src, as_bytes(packet));
      CHECK(a->is_start_send_invoked());
      CHECK_FALSE(b->is_start_send_invoked());

      relay.on_session_sent();
      CHECK(peer.is_start_recv_invoked());
    }
  }


  SECTION("tick: erase invalidated sessions")
  {
    // register a
    test_lib::session *a = nullptr;
    {
      std::array packet = { a_id };
      relay.on_client_received(a_src, as_bytes(packet));
      a = test_lib::session::last_created();
      REQUIRE(a != nullptr);
    }

    // register b
    test_lib::session *b = nullptr;
    {
      std::array packet = { b_id };
      relay.on_client_received(b_src, as_bytes(packet));
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
