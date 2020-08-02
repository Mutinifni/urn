#pragma once

/**
 * \file urn/intrusive_stack.hpp
 * Intrusive LIFO
 */

#include <urn/__bits/lib.hpp>


__urn_begin


template <typename T>
using intrusive_stack_hook = T *;


/**
 * Elements of this container must provide member address \a Next that stores
 * opaque data managed by container. Any given time specific hook can be used
 * only to store element in single container. Same hook can be used to store
 * element in different containers at different times. If application needs to
 * store element in multiple containers same time, it needs to provide
 * multiple hooks, one per owner.
 *
 * Being intrusive, container itself does not deal with node allocation. It is
 * application's responsibility to handle node management and make sure that
 * while in container, element is kept alive and it's hook is not interfered
 * with. Also, pushing and popping elements into/from container does not copy
 * them, just hooks/unhooks using specified member \a Next.
 *
 * Usage:
 * \code
 * class foo
 * {
 *   urn::intrusive_stack_hook<foo> next;
 *   int a;
 *   char b;
 * };
 * urn::intrusive_stack<&foo::next> s;
 *
 * foo f;
 * s.push(&f);
 *
 * auto fp = s.try_pop(); // fp == &f
 * \endcode
 */
template <auto Next>
class intrusive_stack
{
private:

  template <typename T, typename Hook, Hook T::*Member>
  static T type_infer_helper (const intrusive_stack<Member> *);


public:

  using value_type = decltype(
    type_infer_helper(static_cast<intrusive_stack<Next> *>(nullptr))
  );


  intrusive_stack () noexcept = default;
  ~intrusive_stack () noexcept = default;

  intrusive_stack (const intrusive_stack &) = delete;
  intrusive_stack &operator= (const intrusive_stack &) = delete;


  void push (value_type *node) noexcept
  {
    node->*Next = top_;
    top_ = node;
  }


  value_type *try_pop () noexcept
  {
    auto node = top_;
    if (node)
    {
      top_ = top_->*Next;
    }
    return node;
  }


  value_type *top () const noexcept
  {
    return top_;
  }


  bool empty () const noexcept
  {
    return top_ == nullptr;
  }


private:

  value_type *top_ = nullptr;
};


__urn_end
