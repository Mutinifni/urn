#include <urn/intrusive_stack.hpp>
#include <urn/common.test.hpp>


namespace {


TEST_CASE("intrusive_stack")
{
  struct foo
  {
    urn::intrusive_stack_hook<foo> hook{};
    using stack = urn::intrusive_stack<&foo::hook>;
  };

  foo::stack stack{};
  CHECK(stack.empty());
  CHECK(stack.top() == nullptr);
  CHECK(stack.try_pop() == nullptr);


  SECTION("single_push_pop")
  {
    foo f;
    stack.push(&f);
    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f);
    CHECK(stack.try_pop() == &f);
  }


  SECTION("multiple_push_pop")
  {
    foo f1, f2, f3;
    stack.push(&f1);
    stack.push(&f2);
    stack.push(&f3);

    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f3);
    CHECK(stack.try_pop() == &f3);

    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f2);
    CHECK(stack.try_pop() == &f2);

    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f1);
    CHECK(stack.try_pop() == &f1);
  }


  SECTION("interleaved_push_pop")
  {
    // push 1, push 2
    foo f1, f2, f3;
    stack.push(&f1);
    stack.push(&f2);

    // pop 2
    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f2);
    CHECK(stack.try_pop() == &f2);

    // push 3
    stack.push(&f3);

    // pop 3, push 3
    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f3);
    CHECK(stack.try_pop() == &f3);
    stack.push(&f3);

    // pop 3
    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f3);
    CHECK(stack.try_pop() == &f3);

    // pop 1
    REQUIRE_FALSE(stack.empty());
    CHECK(stack.top() == &f1);
    CHECK(stack.try_pop() == &f1);
  }


  CHECK(stack.empty());
  CHECK(stack.top() == nullptr);
  CHECK(stack.try_pop() == nullptr);
}


} // namespace
