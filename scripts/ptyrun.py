from ptyprocess import PtyProcess
from sys import argv, stdout

import getopt

if __name__ == '__main__':
    ofile = None

    try:
        opts, args = getopt.getopt(argv[1:], 'o:')
    except getopt.GetoptError:
        usage()

    for o, a in opts:
        if o == '-o':
            ofile = open(a.strip(), 'w')

    pp = PtyProcess.spawn(args)
    if ofile != None:
        oset = (stdout, ofile)
    else:
        oset = (stdout,)
    while True:
        line = pp.readline().replace('\r', '')
        for of in oset:
            of.write(line)
            of.flush()
