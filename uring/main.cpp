#include <uring/relay.hpp>

int main(int argc, const char* argv[]) {
  urn_uring::config config{argc, argv};
  urn_uring::relay relay{config};
  return relay.run();
}
