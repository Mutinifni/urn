#include <urn/endpoint_v4.hpp>


__urn_begin


namespace
{

/**
 * Fowler-Noll-Vo hashing function for 64bit result.
 *
 * This implementation is copied from it's corresponding homepage at
 * http://www.isthe.com/chongo/tech/comp/fnv/.
 */
template <typename It>
constexpr uint64_t fnv_1a_64 (It first, It last,
  uint64_t h = 0xcbf29ce484222325ULL)
{
  while (first != last)
  {
    h ^= *first++;
    h += (h << 1) + (h << 4) + (h << 5) + (h << 7) + (h << 8) + (h << 40);
  }
  return h;
}

} // namespace


size_t endpoint_v4::hash () const noexcept
{
  uint32_t data[] =
  {
    native().sin_addr.s_addr,
    native().sin_port,
  };
  return fnv_1a_64(data, data + sizeof(data)/sizeof(data[0]));
}

__urn_end
