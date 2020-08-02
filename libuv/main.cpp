#include <libuv/relay.hpp>


int main ()
{
  urn_libuv::config config{};
  urn_libuv::relay relay{config};
  return relay.run();
}
