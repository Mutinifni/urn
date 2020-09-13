#include <urn/mutex.hpp>
#include <urn/common.test.hpp>
#include <type_traits>


namespace {


TEMPLATE_TEST_CASE("shared_mutex", "",
  urn::shared_mutex<false>,
  urn::shared_mutex<true>)
{
  TestType mutex;

  SECTION("lock_guard")
  {
    std::lock_guard lock{mutex};
  }

  SECTION("double lock")
  {
    if constexpr (std::is_same_v<TestType, urn::shared_mutex<false>>)
    {
      mutex.lock();
      mutex.lock();
    }
  }
}


} // namespace
