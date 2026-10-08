#!/usr/bin/env python3
"""make_disc.py dir: writes dir/disc.cue and dir/disc.bin, a small Mode 2 data
track plus an audio track, so CD-ROM commands have something to read."""
import os
import random
import sys

work = sys.argv[1]
rng = random.Random(1)


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


data = bytearray()
for lba in range(300):
    pos = lba + 150
    data += b'\x00' + b'\xff' * 10 + b'\x00' + bytes([bcd(pos // 4500), bcd((pos // 75) % 60), bcd(pos % 75), 2])
    data += bytes(8) + bytes(rng.getrandbits(8) for _ in range(2352 - 24))
audio = bytes(rng.getrandbits(8) for _ in range(2352 * 150))

with open(os.path.join(work, 'disc.bin'), 'wb') as f:
    f.write(data + audio)
with open(os.path.join(work, 'disc.cue'), 'w') as f:
    f.write('FILE "disc.bin" BINARY\n'
            '  TRACK 01 MODE2/2352\n'
            '    INDEX 01 00:00:00\n'
            '  TRACK 02 AUDIO\n'
            '    INDEX 01 00:04:00\n')
