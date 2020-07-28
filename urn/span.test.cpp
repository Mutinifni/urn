#include <urn/span.hpp>
#include <urn/common.test.hpp>
#include <array>
#include <vector>


namespace {


constexpr ptrdiff_t N = 4;


TEMPLATE_TEST_CASE("span", "",
  (urn::span<uint8_t, N>),
  (urn::span<uint16_t, N>),
  (urn::span<uint32_t, N>),
  (urn::span<uint64_t, N>),
  (urn::span<uint8_t, urn::dynamic_extent>),
  (urn::span<uint16_t, urn::dynamic_extent>),
  (urn::span<uint32_t, urn::dynamic_extent>),
  (urn::span<uint64_t, urn::dynamic_extent>)
)
{
  using T = typename TestType::value_type;
  constexpr auto extent = TestType::extent;

  constexpr auto size_bytes = N * sizeof(T);

  T array[N] = { 1, 2, 3, 4 };
  const auto &const_array = array;

  std::array<T, N> std_array = { 1, 2, 3, 4 };
  const auto &const_std_array = std_array;

  std::vector<T> std_vector{array, array + N};
  const auto &const_std_vector = std_vector;


  SECTION("ctor")
  {
    constexpr auto empty_extent = extent == urn::dynamic_extent
      ? urn::dynamic_extent
      : 0
    ;
    urn::span<T, empty_extent> span;
    CHECK(span.empty());
    CHECK(span.size() == 0U);
    CHECK(span.size_bytes() == 0U);
    CHECK(span.data() == nullptr);
  }


  SECTION("ctor_with_ptr_and_count")
  {
    auto span = urn::span<T, extent>(array, N);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_const_ptr_and_count")
  {
    auto span = urn::span<const T, extent>(const_array, N);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_range")
  {
    auto span = urn::span<T, extent>(array, array + N);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_const range")
  {
    auto span = urn::span<const T, extent>(const_array, const_array + N);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_array")
  {
    auto span = urn::span<T, extent>(array);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_const_array")
  {
    auto span = urn::span<const T, extent>(const_array);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_std_array")
  {
    auto span = urn::span<T, extent>(std_array);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == std_array.data());
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_const_std_array")
  {
    auto span = urn::span<const T, extent>(const_std_array);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == const_std_array.data());
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_std_vector")
  {
    auto span = urn::span<T, extent>(std_vector);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == std_vector.data());
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_const_std_vector")
  {
    auto span = urn::span<const T, extent>(const_std_vector);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == const_std_vector.data());
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_same_span")
  {
    auto other = urn::span<T, extent>(array);
    urn::span<const T, extent> span = other;
    CHECK(span.extent == extent);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("ctor_with_different_span")
  {
    constexpr auto different_extent = extent == urn::dynamic_extent
      ? N
      : urn::dynamic_extent
      ;
    auto other = urn::span<T, different_extent>(array);
    urn::span<const T, extent> span = other;
    CHECK(span.extent == extent);
    CHECK_FALSE(span.empty());
    CHECK(span.data() == array);
    CHECK(span.size() == N);
    CHECK(span.size_bytes() == size_bytes);
  }


  SECTION("index")
  {
    std::vector<T> data;
    auto span = urn::span<T, extent>(std_vector);
    for (auto index = 0U;  index < span.size();  ++index)
    {
      data.emplace_back(span[index]);
    }
    CHECK(std_vector == data);
  }


  SECTION("iterator")
  {
    std::vector<T> data;
    auto span = urn::span<T, extent>(std_vector);
    for (auto it = span.begin();  it != span.end();  ++it)
    {
      data.emplace_back(*it);
    }
    CHECK(std_vector == data);
  }


  SECTION("reverse_iterator")
  {
    std::vector<T> data;
    auto span = urn::span<T, extent>(std_vector);
    for (auto it = span.rbegin();  it != span.rend();  ++it)
    {
      data.emplace_back(*it);
    }
    auto reverse_data = std_vector;
    std::reverse(reverse_data.begin(), reverse_data.end());
    CHECK(reverse_data == data);
  }


  SECTION("first_dynamic")
  {
    auto span = urn::span<T, extent>(array);
    auto first_half = span.first(N / 2);
    CHECK(first_half.size() == N / 2);

    std::vector<T>
      expected(array, array + N / 2),
      data(first_half.begin(), first_half.end());
    CHECK(data == expected);
  }


  SECTION("first_static")
  {
    auto span = urn::span<T, extent>(array);
    auto first_half = span.template first<N / 2>();
    CHECK(first_half.size() == N / 2);

    std::vector<T>
      expected(array, array + N / 2),
      data(first_half.begin(), first_half.end());
    CHECK(data == expected);
  }


  SECTION("last_dynamic")
  {
    auto span = urn::span<T, extent>(array);
    auto last_half = span.last(N / 2);
    CHECK(last_half.size() == N / 2);

    std::vector<T>
      expected(array + N / 2, array + N),
      data(last_half.begin(), last_half.end());
    CHECK(data == expected);
  }


  SECTION("last_static")
  {
    auto span = urn::span<T, extent>(array);
    auto last_half = span.template last<N / 2>();
    CHECK(last_half.size() == N / 2);

    std::vector<T>
      expected(array + N / 2, array + N),
      data(last_half.begin(), last_half.end());
    CHECK(data == expected);
  }


  SECTION("subspan_dynamic")
  {
    auto span = urn::span<T, extent>(array);
    auto middle = span.subspan(1, N - 2);
    CHECK(middle.size() == N - 2);

    std::vector<T>
      expected(array + 1, array + N - 1),
      data(middle.begin(), middle.end());
    CHECK(data == expected);
  }


  SECTION("subspan_static")
  {
    auto span = urn::span<T, extent>(array);
    auto middle = span.template subspan<1, N - 2>();
    CHECK(middle.size() == N - 2);

    std::vector<T>
      expected(array + 1, array + N - 1),
      data(middle.begin(), middle.end());
    CHECK(data == expected);
  }


  SECTION("subspan_until_end_dynamic")
  {
    auto span = urn::span<T, extent>(array);
    auto middle = span.subspan(1);
    CHECK(middle.size() == N - 1);

    std::vector<T>
      expected(array + 1, array + N),
      data(middle.begin(), middle.end());
    CHECK(data == expected);
  }


  SECTION("subspan_until_end_static")
  {
    auto span = urn::span<T, extent>(array);
    auto middle = span.template subspan<1>();
    CHECK(middle.size() == N - 1);

    std::vector<T>
      expected(array + 1, array + N),
      data(middle.begin(), middle.end());
    CHECK(data == expected);
  }


  SECTION("as_bytes")
  {
    auto span = urn::as_bytes(urn::span<T, extent>(array));
    CHECK(std::is_const_v<typename decltype(span)::element_type>);
    CHECK(span.data() == reinterpret_cast<const std::byte *>(array));
  }


  SECTION("as_writable_bytes")
  {
    auto span = urn::as_writable_bytes(urn::span<T, extent>(array));
    CHECK_FALSE(std::is_const_v<typename decltype(span)::element_type>);
    CHECK(span.data() == reinterpret_cast<std::byte *>(array));
  }
}


} // namespace
