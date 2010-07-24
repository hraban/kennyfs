'''Parse trace output from kennyfs and scan it for memory leaks.'''
import re
import sys

USAGE = '''Usage:

    %(name)s [-d] [DEBUG_FILE]

If the debug file is omitted or set to '-', stdin is read instead of a file.
Option -d enables debug mode.

Example:

    $ kennyfs -d mountpoint/ &> debug_output
    $ %(name)s debug_output
    ...

''' % dict(name=sys.argv[0])
RE_MALLOC = re.compile(r'kfs_[cm]alloc.*0x(\w+)')
RE_FREE = re.compile(r'kfs_free.*0x(\w+)')
RE_TIMESTAMP = re.compile(r'\d{10}\.\d{6} ')

def scan(f, debug=False):
    stack = []
    mallocs = {}
    totalmallocs = 0
    for linenum, line in enumerate(f):
        line = line.strip()
        linenum += 1
        if debug:
            print >> sys.stderr, '%d: %s' % (linenum, line)
        # If timestamps are printed, ignore them.
        if RE_TIMESTAMP.match(line):
            line = line[18:]
        if line.startswith('[kfs_trace]'):
            stripped = line[len('[kfs_trace] '):]
            if stripped.endswith('enter'):
                stack.append(stripped[:-len(': enter')])
            else:
                assert(stripped.endswith('return'))
                exitf = stack.pop()
        elif line.startswith('[kfs_debug] kfs_memory.c:'):
            m = RE_MALLOC.search(line)
            if m is not None:
		totalmallocs += 1
                memaddr = int(m.group(1), 16)
                try:
                    func = stack[-1]
                except IndexError:
                    sys.exit('Malloc outside function at line %d, corrupt '
                             'stack or trace output not enabled.' % linenum)
                assert(memaddr not in mallocs)
                mallocs[memaddr] = (linenum, func)
                continue
            m = RE_FREE.search(line)
            if m is not None:
                memaddr = int(m.group(1), 16)
                del mallocs[memaddr]
                continue
    if debug:
	print >> sys.stderr, 'Total number of allocations: %d' % totalmallocs
    if mallocs:
        print 'Mallocs that were never freed:'
        print
        print 'Line number in debug output / last function entry before malloc.'
        for info in sorted(mallocs.itervalues()):
            print '%d\t%s' % info
        print 'Total memory leaks: %d' % len(mallocs)
        sys.exit(1)
    else:
        print 'No memory leaks detected.'

def main():
    debug = False
    if '-d' in sys.argv:
        sys.argv.remove('-d')
        debug = True
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
        scan(f, debug)

if __name__ == '__main__':
    main()
