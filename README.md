dns-echo
=======================================================================

This is a program that replies to DNS packets with the same packet
with the response flags modified according to RFC 1035.

It is intended to provide a baseline measure for system throughput
based on various socket reading APIs, specified with the `-m` flag:

 *  `b` : blocking I/O
 *  `n` : non-blocking I/O (busy spin)
 *  `m` : mmsg calls (multiple messages per system call)
 *  `p` : use poll(2)
 *  `s` : use select(2)
 *  `l` : use libevent2 (with system specific read calls)

For increased throughput the program can fork multiple children (`-f`)
and/or multiple threads (`-t`).

The program can fix each child or thread to a different CPU core
using the `-a` flag.

If supplied the `-r` flag the program will open a separate file
descriptor per sub-process using the `SO_REUSEPORT` socket option.

On exit via `SIGINT` or `SIGTERM` it will display the number of
packets that were handled by each sub process.

Building
-----------------------------------------------------------------------

The code is designed to run on Linux and uses a few non-portable API
calls.

It requires libevent2 (and its development packages) to be installed.

To install from git, run `autoreconf -i` to build the configure script,
and then run `./configure` followed by `make`.
