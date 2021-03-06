KennyFS is an attempt at an intelligent file-system for SOHO and personal use.

It should first worry about being correct on a wide variety of hardware and
software, then about being easy to use, then about being feature-full and
finally about being fast.

It is, right now, not nearly at a point where it is usable without lots of
effort and knowledge about programming.


## Portability

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


## Installation

At least the following packages are required (package names on Debian family,
may vary from system to system):

- libattr1-dev
- libfuse-dev
- pkg-config

To build the entire package, just run:

    $ make

Two binaries will be created: `kennyfs` and `tcp_server/server`.

There are three specific build variables you may want to influence: log
verbosity, optimisations and self-checking ("assert").

### Self-checking

Several parts of the code are dedicated to checking if nothing weird is going
on (unexpected bugs). They should never fail, if they do the program is wrong
and will be terminated. If you trust the code you can turm them off:

    $ CFLAGS=-DNDEBUG make

This also disables some debugging capabilities and should in general only be
used if resources are dire.

### Log Verbosity

Maximum logging support (tracing function calls) is enabled at compile-time by
default. This allows full run-time flexibility, at the cost of some overhead: a
fine trade-off in most situations.

To disable support up to a certain loggin level entirely, define the
corresponding `KFS_MINLOG_<LEVEL>` constant. This will leave out log messages
lower than that level at compile time, resulting in a smaller (and hopefully
faster) resulting binary:

    $ CFLAGS=-DKFS_MINLOG_<LEVEL> make

Where `<LEVEL>` is one of SILENT, ERROR, WARNING, INFO, DEBUG, TRACE. The default
is TRACE (or WARNING, if `-DNDEBUG` is specified).

### Optimisations

By default, full optimisations (level 3) are turned on. This can be influenced
with the `KFS_O` variable:

    $ KFS_O=1 make

Use 0 to turn them off completely.


## Configuration

The program will look for a configuration file in ~/.kennyfs.ini, unless you
specify a location with `-o kfsconf=/path/to/kennyfs.ini`. The configuration
syntax is standard ini format, see kennyfs.ini in the repository for an example.

Every section indicates a brick, which can link to other bricks through the
subvolumes option. The type option is the only mandatory one.

    [mybrick]
    type = sometype
    subvolumes = brick1, brick2
    some_option = some_value

The available brick types are:

__pass__: pass all operations through to another brick. only useful for debugging
and example purposes.
- subvolumes: 1
- options: none

__posix__: use your local filesystem as a kennyfs brick.
- subvolumes: 0
- options: path = /path/to/dir

__cache__: cache results from one brick in another brick, speed repeating
operations up.  requires extended attributes on the cache node to do anything
meaningful!
- subvolumes: 2: 1 is the source, 2 is the cache.
- options: none

__tcp__: connect to a kennyfs server through tcp.
- subvolumes: 0
- options:
  - hostname = server.example.com
  - port = 12345

__mirror__: copy operations to multiple subvolumes. compare loosely to RAID 1
(many technical differences, though!).
- subvolumes: 1 or more (the first one is used for read-only operations)
- options: none


## Usage

Once the configuration file is in place, mount a kennyfs tree like this (must be
run in the source directory!):

    $ ./kennyfs /path/to/mount-dir

If you want debugging output:

    $ ./kennyfs -d ...

If you want to specify a custom configuration file:

    $ ./kennyfs -o kfsconf=myconf.ini ...


## Development

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

## License

Licensing and copyright information can be found in the LICENSE file
