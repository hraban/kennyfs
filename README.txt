At least the following packages are required (package names on Debian family,
may vary from system to system):

- libattr1-dev
- libfuse-dev
- pkg-config

To build the entire package, just run:

  $ make

To build it with additional debugging:

  $ CFLAGS=-DKFS_LOG_<LEVEL> make

Where <LEVEL> is one of SILENT, ERROR, WARNING, INFO, DEBUG, TRACE.

Two binaries will be created: ./kennyfs and ./tcp_server/server.
