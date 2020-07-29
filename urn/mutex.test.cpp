#include <urn/mutex.hpp>
#include <urn/common.test.hpp>
#include <type_traits>


namespace {


TEMPLATE_TEST_CASE("mutex", "",
  urn::mutex<false>,
  urn::mutex<true>)
{
  TestType mutex;

  SECTION("lock_guard")
  {
    std::lock_guard lock{mutex};
  }

  SECTION("double lock")
  {
    if constexpr (std::is_same_v<TestType, urn::mutex<false>>)
    {
      mutex.lock();
      mutex.lock();
    }
  }
}


} // namespace
