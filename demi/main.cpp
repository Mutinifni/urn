#include <demi/relay.hpp>
#include <demi/dmtr/libos.h>
#include <demi/dmtr/fail.h>
#include <csignal>

static void stop_all(int signo) {
	printf("recv packets: %lu\n", recv_packets);
	printf("send packets: %lu %d\n", num_packets, signo);
    printf("total_time: %f s\n", (rdtscp(NULL) - start_time) / (2.5 * 1e9));
    exit(0);
}

int main(int argc, char* argv[]) {
  //DMTR_OK(dmtr_init(argc, argv));
  dmtr_init(argc, argv);
  urn_demi::config config{argc, (const char**)argv};
  urn_demi::relay relay{config};

  if (std::signal(SIGINT, stop_all) == SIG_ERR)
	  printf("can't catch SIGINT");
  if (std::signal(SIGTERM, stop_all) == SIG_ERR)
	  printf("can't catch SIGTERM");

  return relay.run();
}
