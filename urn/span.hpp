#pragma once

/**
 * \file urn/span.hpp
 * Temporary std::span implementation until gcc, clang & msvc all have it
 */

#include <urn/__bits/lib.hpp>
#include <array>
#include <cassert>
#include <type_traits>


__urn_begin


constexpr ptrdiff_t dynamic_extent = -1;
template <typename T, ptrdiff_t Extent = dynamic_extent> class span;


namespace __bits
{

template <typename>
struct is_span_t: public std::false_type {};

template <typename T, ptrdiff_t Extent>
struct is_span_t<span<T, Extent>>: public std::true_type {};

template <typename T>
constexpr bool is_span_v = is_span_t<std::remove_cv_t<T>>::value;

template <typename T>
struct is_std_array_t: public std::false_type {};

template <typename T, size_t Size>
struct is_std_array_t<std::array<T, Size>>: public std::true_type {};

template <typename T>
constexpr bool is_std_array_v = is_std_array_t<std::remove_cv_t<T>>::value;

template <typename Container, typename Element, typename = void>
struct is_span_compatible_container_t: public std::false_type {};

template <typename Container, typename Element>
struct is_span_compatible_container_t
<
  Container,
  Element,
  std::void_t
  <
    std::enable_if_t<!is_span_v<Container>>,
    std::enable_if_t<!is_std_array_v<Container>>,
    std::enable_if_t<!std::is_array_v<Container>>,
    decltype(std::data(std::declval<Container>())),
    decltype(std::size(std::declval<Container>())),
    std::enable_if_t
    <
      std::is_convertible_v
      <
        std::remove_pointer_t<decltype(std::data(std::declval<Container &>()))>(*)[],
        Element(*)[]
      >
    >
  >
>: public std::true_type {};

template <typename Container, typename Element>
constexpr bool is_span_compatible_container_v = is_span_compatible_container_t<Container, Element>::value;

template <typename U, typename T>
constexpr bool is_span_compatible_span_v = std::is_convertible_v<U(*), T(*)>;

} // namespace __bits


template <typename T, ptrdiff_t Extent>
class span
{
public:

  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using index_type = ptrdiff_t;
  using difference_type = ptrdiff_t;
  using pointer = T *;
  using reference = T &;
  using const_pointer = const T *;
  using const_reference = const T &;
  using iterator = pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;


  static_assert(Extent >= 0);
  static constexpr index_type extent = Extent;


  constexpr span () noexcept
    : ptr_{nullptr}
  {
    static_assert(Extent == 0);
  }


  constexpr span (const span &) noexcept = default;
  constexpr span &operator= (const span &) noexcept = default;


  constexpr span (pointer ptr, index_type count)
    : ptr_{ptr}
  {
    (void)count;
    assert(Extent == count);
  }


  constexpr span (pointer first, pointer last)
    : ptr_{first}
  {
    (void)first;
    (void)last;
    assert(Extent == std::distance(first, last));
  }


  template <size_t Size>
  constexpr span (element_type (&array)[Size]) noexcept
    : ptr_{array}
  {
    static_assert(Extent == Size);
  }


  template <size_t Size>
  constexpr span (std::array<value_type, Size> &array) noexcept
    : ptr_{array.data()}
  {
    static_assert(Extent == Size);
  }


  template <size_t Size>
  constexpr span (const std::array<value_type, Size> &array) noexcept
    : ptr_{array.data()}
  {
    static_assert(Extent == Size);
  }


  template <typename Container,
    std::enable_if_t<
      __bits::is_span_compatible_container_v<Container, T>,
      int
    > = 0
  >
  constexpr span (Container &container)
    : ptr_{std::data(container)}
  {
    assert(Extent == std::size(container));
  }


  template <typename Container,
    std::enable_if_t<
      __bits::is_span_compatible_container_v<const Container, T>,
      int
    > = 0
  >
  constexpr span (const Container &container)
    : ptr_{std::data(container)}
  {
    assert(Extent == std::size(container));
  }


  template <typename U,
    std::enable_if_t<
      __bits::is_span_compatible_span_v<U, element_type>,
      int
    > = 0
  >
  constexpr span (const span<U, Extent> &that) noexcept
    : ptr_{that.data()}
  { }


  template <typename U,
    std::enable_if_t<
      __bits::is_span_compatible_span_v<U, element_type>,
      int
    > = 0
  >
  constexpr span (const span<U, dynamic_extent> &that) noexcept
    : ptr_{that.data()}
  {
    assert(Extent == that.size());
  }


  constexpr bool empty () const noexcept
  {
    return size() == 0;
  }


  constexpr pointer data () const noexcept
  {
    return ptr_;
  }


  constexpr reference operator[] (index_type index) const noexcept
  {
    assert(0 <= index && index < size());
    return ptr_[index];
  }


  constexpr index_type size () const noexcept
  {
    return Extent;
  }


  constexpr index_type size_bytes () const noexcept
  {
    return size() * sizeof(element_type);
  }


  constexpr iterator begin () const noexcept
  {
    return data();
  }


  constexpr iterator end () const noexcept
  {
    return data() + size();
  }


  constexpr reverse_iterator rbegin () const noexcept
  {
    return reverse_iterator(end());
  }


  constexpr reverse_iterator rend () const noexcept
  {
    return reverse_iterator(begin());
  }


  constexpr span<element_type, dynamic_extent> first (index_type count) const noexcept
  {
    assert(0 <= count && count <= size());
    return {data(), count};
  }


  template <ptrdiff_t Count>
  constexpr span<element_type, Count> first () const noexcept
  {
    static_assert(Count >= 0);
    static_assert(Count <= Extent);
    return {data(), Count};
  }


  constexpr span<element_type, dynamic_extent> last (index_type count) const noexcept
  {
    assert(0 <= count && count <= size());
    return {data() + size() - count, count};
  }


  template <ptrdiff_t Count>
  constexpr span<element_type, Count> last () const noexcept
  {
    static_assert(Count >= 0);
    static_assert(Count <= Extent);
    return {data() + size() - Count, Count};
  }


  constexpr span<element_type, dynamic_extent> subspan (index_type offset,
    index_type count = dynamic_extent) const noexcept
  {
    assert(0 <= offset && offset <= size());
    assert((0 <= count && count <= size()) || count == dynamic_extent);
    return {data() + offset, count == dynamic_extent ? size() - offset : count};
  }


  template <ptrdiff_t Offset, ptrdiff_t Count = dynamic_extent>
  constexpr auto subspan () const noexcept
    -> span<element_type, (Count == dynamic_extent ? Extent - Offset : Count)>
  {
    assert(0 <= Offset && Offset <= size());
    return {data() + Offset, Count == dynamic_extent ? size() - Offset : Count};
  }


  auto as_bytes () const noexcept
  {
    return span<const std::byte, Extent * sizeof(element_type)>
    {
      reinterpret_cast<const std::byte *>(data()),
        size_bytes()
    };
  }


  auto as_writable_bytes () const noexcept
  {
    return span<std::byte, Extent * sizeof(element_type)>
    {
      reinterpret_cast<std::byte *>(data()),
        size_bytes()
    };
  }


private:

  pointer ptr_{};
};


template <typename T>
class span<T, dynamic_extent>
{
public:

  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using index_type = ptrdiff_t;
  using difference_type = ptrdiff_t;
  using pointer = T *;
  using reference = T &;
  using const_pointer = const T *;
  using const_reference = const T &;
  using iterator = pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;


  static constexpr index_type extent = dynamic_extent;


  constexpr span () noexcept
    : ptr_{nullptr}
    , count_{0}
  { }


  constexpr span (const span &) noexcept = default;
  constexpr span &operator= (const span &) noexcept = default;


  constexpr span (pointer ptr, index_type count)
    : ptr_{ptr}
    , count_{count}
  { }


  constexpr span (pointer first, pointer last)
    : ptr_{first}
    , count_{std::distance(first, last)}
  { }


  template <size_t N>
  constexpr span (element_type (&array)[N]) noexcept
    : ptr_{array}
    , count_{N}
  { }


  template <size_t N>
  constexpr span (std::array<value_type, N> &array) noexcept
    : ptr_{array.data()}
    , count_{N}
  { }


  template <size_t N>
  constexpr span (const std::array<value_type, N> &array) noexcept
    : ptr_{array.data()}
    , count_{N}
  { }


  template <typename Container,
    std::enable_if_t<
      __bits::is_span_compatible_container_v<Container, T>,
      int
    > = 0
  >
  constexpr span (Container &container)
    : ptr_{std::data(container)}
    , count_{static_cast<index_type>(std::size(container))}
  { }


  template <typename Container,
    std::enable_if_t<
      __bits::is_span_compatible_container_v<const Container, T>,
      int
    > = 0
  >
  constexpr span (const Container &container)
    : ptr_{std::data(container)}
    , count_{static_cast<index_type>(std::size(container))}
  { }


  template <typename U, ptrdiff_t OtherExtent,
    std::enable_if_t<
      __bits::is_span_compatible_span_v<U, element_type>,
      int
    > = 0
  >
  constexpr span (const span<U, OtherExtent> &that) noexcept
    : ptr_{that.data()}
    , count_{that.size()}
  { }


  constexpr bool empty () const noexcept
  {
    return size() == 0;
  }


  constexpr pointer data () const noexcept
  {
    return ptr_;
  }


  constexpr reference operator[] (index_type index) const noexcept
  {
    assert(0 <= index && index < size());
    return ptr_[index];
  }


  constexpr index_type size () const noexcept
  {
    return count_;
  }


  constexpr index_type size_bytes () const noexcept
  {
    return size() * sizeof(element_type);
  }


  constexpr iterator begin () const noexcept
  {
    return data();
  }


  constexpr iterator end () const noexcept
  {
    return data() + size();
  }


  constexpr reverse_iterator rbegin () const noexcept
  {
    return reverse_iterator(end());
  }


  constexpr reverse_iterator rend () const noexcept
  {
    return reverse_iterator(begin());
  }


  constexpr span<element_type, dynamic_extent> first (index_type count) const noexcept
  {
    assert(0 <= count && count <= size());
    return {data(), count};
  }


  template <ptrdiff_t Count>
  constexpr span<element_type, Count> first () const noexcept
  {
    static_assert(Count >= 0);
    assert(Count <= size());
    return {data(), Count};
  }


  constexpr span<element_type, dynamic_extent> last (index_type count) const noexcept
  {
    assert(0 <= count && count <= size());
    return {data() + size() - count, count};
  }


  template <ptrdiff_t Count>
  constexpr span<element_type, Count> last () const noexcept
  {
    static_assert(Count >= 0);
    assert(Count <= size());
    return {data() + size() - Count, Count};
  }


  constexpr span<element_type, dynamic_extent> subspan (index_type offset,
    index_type count = dynamic_extent) const noexcept
  {
    assert(0 <= offset && offset <= size());
    assert((0 <= count && count <= size()) || count == dynamic_extent);
    return {data() + offset, count == dynamic_extent ? size() - offset : count};
  }


  template <ptrdiff_t Offset, ptrdiff_t Count = dynamic_extent>
  constexpr span<element_type, Count> subspan ()
    const noexcept
  {
    assert(0 <= Offset && Offset <= size());
    assert(Count == dynamic_extent || Offset + Count <= size());
    return {data() + Offset, Count == dynamic_extent ? size() - Offset : Count};
  }


  auto as_bytes () const noexcept
  {
    return span<const std::byte, dynamic_extent>
    {
      reinterpret_cast<const std::byte *>(data()),
        size_bytes()
    };
  }


  auto as_writable_bytes () const noexcept
  {
    return span<std::byte, dynamic_extent>
    {
      reinterpret_cast<std::byte *>(data()),
        size_bytes()
    };
  }


private:

  pointer ptr_{};
  index_type count_{};
};


//
// Deduction guides
//


template <typename T, size_t Size>
span (T (&)[Size]) -> span<T, Size>;

template <typename T, size_t Size>
span (std::array<T, Size> &) -> span<T, Size>;

template <typename T, size_t Size>
span (const std::array<T, Size> &) -> span<const T, Size>;

template <typename Container>
span (Container &) -> span<typename Container::value_type>;

template <typename Container>
span (const Container &) -> span<const typename Container::value_type>;


template <typename T, ptrdiff_t Count>
auto as_bytes (span<T, Count> span) noexcept
  -> decltype(span.as_bytes())
{
  return span.as_bytes();
}


template <typename T, ptrdiff_t Count>
auto as_writable_bytes (span<T, Count> span) noexcept
  -> std::enable_if_t<!std::is_const_v<T>, decltype(span.as_writable_bytes())>
{
  return span.as_writable_bytes();
}


__urn_end
