# UDP relay for network I/O scalability testing

[![Build Status](https://travis-ci.org/svens/urn.svg?branch=master)](https://travis-ci.org/svens/urn)
[![Coverage](https://coveralls.io/repos/github/svens/urn/badge.svg?branch=master)](https://coveralls.io/github/svens/urn?branch=master)


## Compiling and installing

    $ mkdir build && cd build
    $ cmake .. [-Durn_unittests=yes|no] [-Durn_benchmarks=yes|no]
    $ make && make test && make install


## Source tree

The source tree is organised as follows:

    .               Root of source tree
    |- urn          Platform-independent packet relay library
    |- bench        Business logic benchmarks
    |- cmake        CMake modules
    |- extern       External code as git submodules
    `- scripts      Helper scripts
