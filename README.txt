KennyFS is an attempt at an intelligent file-system for SOHO and personal use.

It should first worry about being correct on a wide variety of hardware and
software, then about being easy to use, then about being feature-full and
finally about being fast.

It is, right now, not nearly at a point where it is usable without lots of
effort and knowledge about programming.


== Portability ==

To be fairly honest, I have very little experience with anything like autotools
and ./configure files. The only way to build the program, right now, is by
directly invoking make, which greatly limits the customisability of
installation. In fact, there is no make install: the binary must be invoked
in-place and dynamic libraries can not even be relocated. Hopefully this will
soon change.

Another problem is the limited portability: while the code is theoretically
relatively portable due to its heavy adherence to the POSIX API, there are a ton
of "little things" that cause compilation errors on most machines. The only
platforms that compilation is consistently checked on are:

- GCC on Debian on ARM
- GCC on Ubuntu on x86


== Installation ==

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


== Configuration ==

The program will look for a configuration file in ~/.kennyfs.ini, unless you
specify a location with -o kfsconf=/path/to/kennyfs.ini. The configuration
syntax is standard ini format, see kennyfs.ini in the repository for an example.

Every section indicates a brick, which can link to other bricks through the
subvolumes option. The type option is the only mandatory one.

  [mybrick]
  type = sometype
  subvolumes = brick1, brick2
  some_option = some_value

The available brick types are:

- pass: pass all operations through to another brick. only useful for debugging
  and example purposes.
    subvolumes: 1
    options: none

- posix: use your local filesystem as a kennyfs brick.
    subvolumes: 0
    options: path = /path/to/dir

- cache: cache results from one brick in another brick, speed repeating
  operations up.  requires extended attributes on the cache node to do anything
  meaningful!
    subvolumes: 2: 1 is the source, 2 is the cache.
    options: none

- tcp: connect to a kennyfs server through tcp.
    subvolumes: 0
    options: hostname = server.example.com
             port = 12345

- mirror: copy operations to multiple subvolumes. compare loosely to RAID 1
  (many technical differences, though!).
    subvolumes: 1 or more (the first one is used for read-only operations)
    options: none


== Usage ==

Once the configuration file is in place, mount a kennyfs tree like this (must be
run in the source directory!):

  $ ./kennyfs /path/to/mount-dir

If you want debugging output:

  $ ./kennyfs -d ...

If you want to specify a custom configuration file:

  $ ./kennyfs -o kfsconf=myconf.ini ...


== Development ==

Development is, for the moment, not very structured: there is no clear
roadmap and there is no explicit requirements list: a typical one-person
project. The code is littered with TODO's to identify weak spots and code that
needs more attention.

One very important action item is a good, thorough, harsh, tenacious testing
routine. Threading of FUSE itself, concurrent access by different processes,
there are many possible race conditions. Then there are all those errors that
almost never occur, like memory errors or hard disk space running out, which
definitely need thorough checking. Finally, the issue of varying architectures:
what works on x86 can fail hard on ARM, and even different operating systems.
An easy to invoke, regularly used testing script is needed badly. Right now,
there is nothing.
