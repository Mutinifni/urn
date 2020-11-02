#include <mmsg/relay.hpp>

int main(int argc, const char* argv[]) {
  urn_mmsg::config config{argc, argv};
  urn_mmsg::relay relay{config};
  return relay.run();
}
