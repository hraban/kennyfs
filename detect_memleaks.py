'''Parse trace output from kennyfs and scan it for memory leaks.'''
import re
import sys

RE_MALLOC = re.compile(r'kfs_malloc.*0x(\w+)')
RE_FREE = re.compile(r'kfs_free.*0x(\w+)')
USAGE = '''Usage:

    %(name)s [-d] [DEBUG_FILE]

If the debug file is omitted or set to '-', stdin is read instead of a file.
Option -d enables debug mode.

Example:

    $ kennyfs -d mountpoint/ &> debug_output
    $ %(name)s debug_output
    ...

''' % dict(name=sys.argv[0])

def main():
    stack = []
    mallocs = {}
    debug = False
    if '-d' in sys.argv:
        sys.argv.remove('-d')
        debug = True
    if '-h' in sys.argv or '--help' in sys.argv or len(sys.argv) > 2:
        print __doc__
        print
        print USAGE
        sys.exit(0)
    try:
        output = sys.argv[1]
    except IndexError:
        output = '-'
    if output == '-':
        f = sys.stdin
    else:
        f = open(output, 'rt')
    for linenum, line in enumerate(f):
        line = line.strip()
        linenum += 1
        if debug:
            print '%d: %s' % (linenum, line)
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

if __name__ == '__main__':
    main()
