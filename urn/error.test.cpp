#include <urn/error.hpp>
#include <urn/common.test.hpp>
#include <type_traits>


namespace {


TEST_CASE("error")
{
  SECTION("errc")
  {
    #define __urn_errc_value(code, message) urn::errc::code,
      std::error_code ec = GENERATE(values({__urn_errc(__urn_errc_value)}));
    #undef __urn_errc_value
    CAPTURE(ec);

    SECTION("message")
    {
      CHECK_FALSE(ec.message().empty());
      CHECK(ec.message() != "unknown");
      CHECK(ec.category() == urn::error_category());
      CHECK(ec.category().name() == std::string{"urn"});
    }
  }


  SECTION("message_bad_alloc")
  {
    std::error_code ec = urn::errc::__0;
    urn_test::bad_alloc_once x;
    CHECK_THROWS_AS(ec.message(), std::bad_alloc);
  }


  SECTION("unknown")
  {
    std::error_code ec = static_cast<urn::errc>(
      std::numeric_limits<std::underlying_type_t<urn::errc>>::max()
    );
    CHECK(ec.message() == "unknown");
    CHECK(ec.category() == urn::error_category());
    CHECK(ec.category().name() == std::string{"urn"});
  }
}


} // namespace
