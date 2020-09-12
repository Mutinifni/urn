#include <libuv/relay.hpp>
#include <exception>
#include <iostream>


int main (int argc, const char *argv[])
{
  try
  {
    urn_libuv::config config{argc, argv};
    urn_libuv::relay relay{config};
    return relay.run();
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
