'''Parse trace output from kennyfs and scan it for memory leaks.'''
import re
import sys

USAGE = '''Usage:

    %(name)s [-d] [-v] [DEBUG_FILE]

If the debug file is omitted or set to '-', stdin is read instead of a file.
Option -v enables verbose mode, -d enables debug mode.

Example:

    $ kennyfs -d mountpoint/ &> debug_output
    $ %(name)s debug_output
    ...

''' % dict(name=sys.argv[0])
RE_MALLOC = re.compile(r'kfs_[cm]alloc.*\b(\d+)\b.*0x(\w+)')
RE_FREE = re.compile(r'kfs_free.*0x(\w+)')
RE_TIMESTAMP = re.compile(r'\d{10}\.\d{6} ')

def scan(f, debuglevel=0):
    stack = []
    mallocs = {}
    totalmallocs = 0
    totalbytes = 0
    # Total memory allocated (but not freed yet) at the current point.
    concurrentbytes = 0
    # Maximum memory allocated at any one point during execution.
    maxconcurrentbytes = 0
    use_trace = True
    for linenum, line in enumerate(f):
        line = line.strip()
        linenum += 1
        if debuglevel > 1:
            print >> sys.stderr, '%d: %s' % (linenum, line)
        # If timestamps are printed, ignore them.
        if RE_TIMESTAMP.match(line):
            line = line[18:]
        if use_trace and line.startswith('[kfs_trace]'):
            stripped = line[len('[kfs_trace] '):]
            if stripped.endswith('enter'):
                stack.append(stripped[:-len(': enter')])
            else:
                assert(stripped.endswith('return'))
                exitf = stack.pop()
        elif line.startswith('[kfs_debug] kfs_memory.c:'):
            m = RE_MALLOC.search(line)
            if m is not None:
                nbytes = int(m.group(1))
                memaddr = int(m.group(2), 16)
                totalmallocs += 1
                totalbytes += nbytes
                concurrentbytes += nbytes
                maxconcurrentbytes = max(maxconcurrentbytes, concurrentbytes)
                if use_trace:
                    try:
                        func = stack[-1]
                    except IndexError:
                        use_trace = False
                        func = None
                else:
                    func = None
                assert(memaddr not in mallocs)
                mallocs[memaddr] = (linenum, func, nbytes)
                continue
            m = RE_FREE.search(line)
            if m is not None:
                memaddr = int(m.group(1), 16)
                malloc = mallocs.pop(memaddr)
                concurrentbytes -= malloc[2]
                continue
    if debuglevel > 0:
        print >> sys.stderr, (
                'Total number of allocations: %d\n'
                'Total number of bytes allocated: %d\n'
                'Maximum number of bytes ever concurrently allocated: %d'
                % (totalmallocs, totalbytes, maxconcurrentbytes))
        print >> sys.stderr
    if mallocs:
        print 'Mallocs that were never freed:'
        print
        print ('Line number in debug output / '
               'bytes / '
               'last function entry before malloc.')
        totalbytes = 0
        for linenum, func, nbytes in sorted(mallocs.itervalues()):
            if func is None:
                func = '<no trace information available>'
            print '%d\t%d\t%s' % (linenum, nbytes, func)
            totalbytes += nbytes
        print 'Total memory leaks: %d (%d bytes).' % (len(mallocs), totalbytes)
        sys.exit(1)
    else:
        print 'No memory leaks detected.'

def main():
    debuglevel = 0
    if '-v' in sys.argv:
        sys.argv.remove('-v')
        debuglevel = 1
    if '-d' in sys.argv:
        sys.argv.remove('-d')
        debuglevel = 2
    if '-h' in sys.argv or '--help' in sys.argv:
        print __doc__
        print
        print USAGE
        sys.exit(0)
    if len(sys.argv) == 1:
        sys.argv.append('-')
    for fname in sys.argv[1:]:
        if fname == '-':
            f = sys.stdin
        else:
            f = open(fname)
        scan(f, debuglevel)

if __name__ == '__main__':
    main()
