# UDP relay for network I/O scalability testing

[![Build](https://github.com/svens/urn/workflows/Build/badge.svg)](https://github.com/svens/urn/actions?query=workflow:Build)
[![Coverage](https://coveralls.io/repos/github/svens/urn/badge.svg)](https://coveralls.io/github/svens/urn)

UDP relay mimicks [TURN](https://tools.ietf.org/html/rfc5766) relay:
* client connects to relay on port 3478 and registers itself using random
  64-bit integer
* peer sends data to port 3479 with same 64-bit integer that client used for
  registering
* relay forwards packets sent by peer to client

This creates simple platform to experiment with different relay network I/O
patterns and threading models. Relaying logic is isolated into very simple
header-only [urn]( https://github.com/svens/urn/blob/master/urn/relay.hpp)
library that can be separately tested and benchmarked.

For testing different I/O and/or threading models, create new experiment
executable sub-project that provides syscall wrappers and invokes business
logic hooks. Business logic is implemented by class [`urn::relay<Library,
MultiThreaded>`](https://github.com/svens/urn/blob/92a415c59cb221159f134a3629ee90664ef5fa2e/urn/relay.hpp#L19).

Library should provide following API:
```cpp
struct Library
{
  using endpoint = /* source/destination endpoint */

  struct packet
  {
    std::byte *data ();
    size_t size ();
  };

  struct client
  {
    // Start receive on client port
    // On completion, invoke relay<Library>::on_client_received()
    void start_receive ();
  };

  struct peer
  {
    // Start receive on peer port
    // On completion, invoke relay<Library>::on_peer_received()
    void start_receive ();
  };

  struct session
  {
    // Construct new session with associated endpoint \a src
    session (const endpoint &src);

    // Start sending \a data to associated endpoint
    // On completion, invoke relay<Library>::on_session_sent()
    void start_send (packet &&p);
  };
};
```


## Compiling and installing

    $ mkdir build && cd build
    $ cmake .. [-Durn_unittests=yes|no] [-Durn_benchmarks=yes|no] [experiment(s)]
    $ make && make test

Experiments:
* `-Durn_libuv=yes|no`
  [libuv](https://github.com/libuv/libuv)-based experiment
  (https://github.com/svens/urn/blob/master/libuv/relay.hpp)

Notes:
* `make` builds all enabled experiments
* `make test` tests only business logic


## Source tree

The source tree is organised as follows:

    .               Root of source tree
    |- urn          Platform-independent packet relay library
    |- bench        Business logic benchmarks
    |- cmake        CMake modules
    |- extern       External code as git submodules
    `- libuv        [libuv](https://github.com/libuv/libuv) based experiment
