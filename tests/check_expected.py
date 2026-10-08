#!/usr/bin/env python3
"""check_expected.py run.log name.expected cases [pairs=N]

Compares a test ROM's results with its expected file. The ROM leaves
0xC0FFEE in RAM[0] when it finished, a checksum per case in
RAM[0x100 + n * 4] and flags per case in RAM[0x200 + n * 4]; run.log holds
the frontend's RAMDUMP lines. With pairs=N, cases 2n and 2n+1 below N run
the same data in a different order and must agree. UPDATE_EXPECTED=1 rewrites the
expected file from this run instead."""
import os
import sys

log, expected, cases = sys.argv[1], sys.argv[2], int(sys.argv[3])
pairs = int(sys.argv[4].split('=')[1]) if len(sys.argv) > 4 else 0
name = os.path.basename(expected).split('.')[0]

words = {}
for line in open(log):
    fields = line.split()
    if len(fields) == 2 and len(fields[0]) == 8:
        words[int(fields[0], 16)] = int(fields[1], 16)

if words.get(0) != 0xC0FFEE:
    sys.exit('%s: the test did not finish (RAM[0] = %#x)' % (name, words.get(0, 0)))

got = ['%d %08x %x' % (c, words[0x100 + c * 4], words.get(0x200 + c * 4, 0)) for c in range(cases)]

if pairs:
    for c in range(0, pairs, 2):
        if words[0x100 + c * 4] != words[0x104 + c * 4]:
            sys.exit('%s: cases %d and %d run the same data but differ' % (name, c, c + 1))

if os.environ.get('UPDATE_EXPECTED') == '1':
    with open(expected, 'w') as f:
        f.write('# case, checksum, flags (see tests/guest/%s.c)\n' % name)
        f.write('\n'.join(got) + '\n')
    sys.exit('%s: wrote %s' % (name, expected))

want = [l.strip() for l in open(expected) if l.strip() and not l.startswith('#')]
if len(want) != cases:
    sys.exit('%s: %s does not list %d cases' % (name, expected, cases))

bad = [(g, w) for g, w in zip(got, want) if g != w]
for g, w in bad:
    print('%s: case got "%s", expected "%s"' % (name, g, w))
if bad:
    sys.exit(1)
print('%s: all %d cases match' % (name, cases))
