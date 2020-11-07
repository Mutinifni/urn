#include <demi/relay.hpp>
#include <demi/dmtr/libos.h>
#include <demi/dmtr/fail.h>

int main(int argc, char* argv[]) {
  DMTR_OK(dmtr_init(argc, argv));
  urn_demi::config config{argc, (const char**)argv};
  urn_demi::relay relay{config};
  return relay.run();
}
