#include <urn/endpoint_v4.hpp>
#include <urn/common.test.hpp>
#include <unordered_map>


namespace {


uint32_t get_native_address () noexcept
{
  return ntohl(INADDR_LOOPBACK);
}

const uint32_t native_address = get_native_address();
const uint32_t native_port = 3478;


void init (sockaddr_storage &ss) noexcept
{
  ss.ss_family = AF_INET;
  auto in = reinterpret_cast<sockaddr_in *>(&ss);
  in->sin_addr.s_addr = htonl(native_address);
  in->sin_port = htons(native_port);
}


TEST_CASE("endpoint_v4")
{
  std::error_code error;

  SECTION("invalid")
  {
    sockaddr_storage ss;
    ss.ss_family = static_cast<decltype(ss.ss_family)>(~AF_INET);
    auto a = urn::as_endpoint_v4(ss, error);
    CHECK(a == nullptr);
    CHECK(error == urn::errc::not_inet_v4_endpoint);
  }

  SECTION("valid")
  {
    sockaddr_storage ss;
    init(ss);
    auto a = urn::as_endpoint_v4(ss, error);
    REQUIRE(static_cast<const void *>(a) == static_cast<const void *>(&ss));
    REQUIRE_FALSE(error);
    CHECK(a->address() == native_address);
    CHECK(a->port() == native_port);
  }

  SECTION("address")
  {
    urn::endpoint_v4 a(native_address, native_port);
    a.address(native_address + 1);
    CHECK(a.address() == native_address + 1);
  }

  SECTION("port")
  {
    urn::endpoint_v4 a(native_address, native_port);
    a.port(native_port + 1);
    CHECK(a.port() == native_port + 1);
  }

  SECTION("equal_to")
  {
    urn::endpoint_v4 a1(native_address, native_port);
    urn::endpoint_v4 a2(native_address, native_port);
    CHECK(a1 == a2);
    CHECK(a2 == a1);

    urn::endpoint_v4 b(native_address + 1, native_port);
    CHECK_FALSE(b == a1);
    CHECK_FALSE(a1 == b);

    urn::endpoint_v4 c(native_address, native_port + 1);
    CHECK_FALSE(c == a1);
    CHECK_FALSE(a1 == c);
    CHECK_FALSE(c == b);
    CHECK_FALSE(b == c);
  }

  SECTION("hash")
  {
    urn::endpoint_v4 a(native_address, native_port);
    urn::endpoint_v4 b(native_address, native_port + 1);
    CHECK(a.hash() != b.hash());
  }

  SECTION("unordered_map")
  {
    urn::endpoint_v4 a(native_address, native_port),
      b(native_address + 1, native_port),
      c(native_address, native_port + 1);

    std::unordered_map<urn::endpoint_v4, int> index =
    {
      { a, 'a' },
      { b, 'b' },
      { c, 'c' },
    };

    CHECK(index[a] == 'a');
    CHECK(index[b] == 'b');
    CHECK(index[c] == 'c');

    urn::endpoint_v4 d(native_address + 1, native_port + 1);
    CHECK(index.count(d) == 0);
  }
}


} // namespace
