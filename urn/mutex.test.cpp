#include <urn/mutex.hpp>
#include <urn/common.test.hpp>
#include <type_traits>


namespace {


TEMPLATE_TEST_CASE("mutex", "",
  urn::mutex<1>,
  urn::mutex<2>)
{
  TestType mutex;

  SECTION("lock_guard")
  {
    std::lock_guard lock{mutex};
  }

  SECTION("double lock")
  {
    if constexpr (std::is_same_v<TestType, urn::mutex<1>>)
    {
      mutex.lock();
      mutex.lock();
    }
  }
}


} // namespace
