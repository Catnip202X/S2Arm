#!/usr/bin/env python3
"""Extract VP9 video streams from CRI USM movies as IVF files (local analysis only; game files never enter the repo).

Usage:
  usm_to_ivf.py <file.usm|archive.cpk> <out_dir> [--list] [--only N[,N...]]

The input may be a single .usm file or a CPK archive that stores USM files uncompressed back to back (e.g.
P4G's data_movie.cpk): every "CRID" chunk starts a movie. For each movie, the @SFV data chunks are written to
<out_dir>/movie_<NN>.ivf, in order. On Switch these payloads are already IVF (32-byte "DKIF" file header in the
first chunk, then one 12-byte IVF frame header + frame per chunk), so the output is those bytes concatenated.
Superframes stay intact: one IVF packet per USM video chunk.

Output contract: each .ivf parses as IVF (fourcc VP90), packet count == number of @SFV data chunks, and
--list prints per movie: index, packets, frame-rate field (fps * 100 from the chunk header), byte size.
Standard library only. Exit codes: 0 ok, 1 no CRID/@SFV found, 2 bad arguments.

USM chunk layout used here (CRI USM, big-endian): 4-byte signature, 4-byte size, then 1 unused byte, 1 byte
payload offset, 2 bytes padding size, 1 byte channel, 2 unused, 1 byte data type (low 2 bits: 0 = data,
1 = header, 2 = section end, 3 = metadata), 4 bytes frame time, 4 bytes frame rate; payload starts at
chunk + 8 + payload offset and is (size - payload offset - padding) bytes long.
"""
import mmap
import os
import re
import struct
import sys

SIGS = (b'CRID', b'@SFV', b'@SFA', b'@ALP', b'@CUE', b'@SBT')


def movies(m):
    starts = [x.start() for x in re.finditer(b'CRID', m)]
    for i, s in enumerate(starts):
        yield i, s, starts[i + 1] if i + 1 < len(starts) else len(m)


def video_chunks(m, start, end):
    p = start
    while p + 32 <= end:
        sig = m[p:p + 4]
        if sig not in SIGS:
            break
        size = struct.unpack('>I', m[p + 4:p + 8])[0]
        hoff = m[p + 9]
        pad = struct.unpack('>H', m[p + 10:p + 12])[0]
        if sig == b'@SFV' and (m[p + 15] & 3) == 0:
            rate = struct.unpack('>I', m[p + 20:p + 24])[0]
            yield m[p + 8 + hoff:p + 8 + size - pad], rate
        p += 8 + size


def main(argv):
    if len(argv) < 3 or argv[1].startswith('-'):
        print('\n'.join(__doc__.strip().splitlines()[2:4]), file=sys.stderr)
        return 2
    src, out = argv[1], argv[2]
    listing = '--list' in argv
    only = None
    if '--only' in argv:
        only = {int(x) for x in argv[argv.index('--only') + 1].split(',')}
    os.makedirs(out, exist_ok=True)
    found = 0
    with open(src, 'rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
        for idx, s, e in movies(m):
            if only is not None and idx not in only:
                continue
            path = os.path.join(out, f'movie_{idx:02d}.ivf')
            packets = 0
            rate = 0
            with open(path, 'wb') as o:
                for data, r in video_chunks(m, s, e):
                    o.write(data)
                    packets += 1
                    rate = rate or r
            if packets == 0:
                os.remove(path)
                continue
            found += 1
            with open(path, 'rb') as chk:
                head = chk.read(32)
            if head[:4] != b'DKIF' or head[8:12] != b'VP90':
                print(f'movie {idx}: payload is not IVF/VP9 (got {head[:4]!r} {head[8:12]!r})', file=sys.stderr)
            if listing:
                print(f'{idx:2d}\tpackets={packets}\trate_x100={rate}\tbytes={os.path.getsize(path)}')
    return 0 if found else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
