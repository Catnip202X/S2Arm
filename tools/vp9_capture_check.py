#!/usr/bin/env python3
"""Read NVDEC VP9 job captures (S2A_NVDEC_VP9_CAPTURE, see compat/tree/src/Ryujinx.Graphics.Nvdec/S2AVp9Capture.cs) and answer
the Phase 1 questions of docs/设计_原生VP9/design_draft.md. Local analysis only: captures and IVF files stay on this machine.

Usage:
  vp9_capture_check.py report <capture_dir> <movie.ivf> [<movie.ivf> ...]
  vp9_capture_check.py compare <baseline_dir> <perturbed_dir>

report: matches each job to a VP9 frame of the given IVF files (superframes split) by its tile data, which is the end of a
frame. Then, per job: is the frame's original header (the bytes before the tile data in the frame) sitting immediately
before the tile data in guest memory, or elsewhere in the captured 64 KiB, or not at all; does BitstreamSize carry
anything past the frame (a superframe index); does the MV read offset equal the previous job's MV write offset.
compare: aligns two runs by job index and tile-data hash and counts jobs whose EntropyProbs or output hashes differ.
Standard library only. Exit codes: 0 ok, 1 nothing to read / no job matched, 2 bad arguments.
"""
import collections
import glob
import hashlib
import os
import struct
import sys

REG_NAMES = ('last_y', 'last_c', 'golden_y', 'golden_c', 'alt_y', 'alt_c', 'out_y', 'out_c',
             'mv_read', 'mv_write', 'counts', 'seg_read', 'seg_write', 'tile_size', 'inbuf')


def read_job(path):
    with open(path, 'rb') as f:
        b = f.read()
    if b[:4] != b'S2V9' or struct.unpack_from('<I', b, 4)[0] != 1:
        raise ValueError(f'{path}: not a version-1 S2V9 record')
    job, n_pic, n_ent, n_reg, n_tile, n_pre = struct.unpack_from('<6I', b, 8)
    p = 32
    pic = b[p:p + n_pic]; p += n_pic
    ent = b[p:p + n_ent]; p += n_ent
    regs = dict(zip(REG_NAMES, struct.unpack_from(f'<{n_reg // 4}I', b, p))); p += n_reg
    tiles = b[p:p + n_tile]; p += n_tile
    pre = b[p:p + n_pre]; p += n_pre
    decoded = b[p]; p += 1
    y, u, v, mv, counts = struct.unpack_from('<5Q', b, p)
    return dict(job=job, pic=pic, ent=ent, regs=regs, tiles=tiles, pre=pre, decoded=decoded,
                out=(y, u, v), mv=mv, counts=counts)


def jobs(d):
    files = sorted(glob.glob(os.path.join(d, 'frame_*.bin')))
    return [read_job(f) for f in files]


def split_superframe(data):
    last = data[-1]
    if last & 0xe0 == 0xc0:
        frames = (last & 7) + 1
        mag = ((last >> 3) & 3) + 1
        size = 2 + mag * frames
        if len(data) >= size and data[-size] == last:
            out, p = [], 0
            for i in range(frames):
                n = int.from_bytes(data[-size + 1 + i * mag:-size + 1 + (i + 1) * mag], 'little')
                out.append(data[p:p + n]); p += n
            return out, size
    return [data], 0


def ivf_frames(path):
    with open(path, 'rb') as f:
        b = f.read()
    if b[:4] != b'DKIF':
        raise ValueError(f'{path}: not IVF')
    p = struct.unpack_from('<H', b, 6)[0]
    while p + 12 <= len(b):
        n = struct.unpack_from('<I', b, p)[0]
        pkt = b[p + 12:p + 12 + n]
        p += 12 + n
        subs, idx = split_superframe(pkt)
        for i, s in enumerate(subs):
            yield s, idx if i == len(subs) - 1 else 0


def report(cap, ivfs):
    js = jobs(cap)
    if not js:
        print('no captures'); return 1
    index = collections.defaultdict(list)
    for path in ivfs:
        for frame, idx in ivf_frames(path):
            index[frame[-8:]].append((frame, idx))
    stats = collections.Counter()
    dist = collections.Counter()
    prev_write = None
    for j in js:
        t = j['tiles']
        # every frame whose end equals the tile data (or tile data + a trailing superframe index of 3..34 bytes);
        # identical tiny frames make this ambiguous, so the candidate whose header is found in guest memory wins
        cands = []
        for cut in (0, *range(3, 35)):
            body = t[:len(t) - cut] if cut else t
            for frame, idx in index.get(body[-8:], ()):
                if frame.endswith(body) and (cut == 0 or cut == idx):
                    cands.append((frame, cut))
            if cands:
                break
        rank = lambda c: 0 if j['pre'].endswith(c[0][:len(c[0]) - (len(t) - c[1])]) else 1 if c[0][:len(c[0]) - (len(t) - c[1])] in j['pre'] else 2
        match = min(cands, key=rank) if cands else None
        if prev_write is not None:
            stats['mv_read_eq_prev_write' if j['regs']['mv_read'] == prev_write else 'mv_read_ne_prev_write'] += 1
        prev_write = j['regs']['mv_write']
        if not match:
            stats['unmatched'] += 1; continue
        frame, cut = match
        stats['matched'] += 1
        stats['superframe_index_inside' if cut else 'no_trailing_bytes'] += 1
        header = frame[:len(frame) - (len(t) - cut)]
        pre = j['pre']
        if pre.endswith(header):
            stats['header_immediately_before'] += 1
        else:
            k = pre.rfind(header)
            if k >= 0:
                stats['header_elsewhere_before'] += 1
                dist[len(pre) - k - len(header)] += 1
            else:
                stats['header_absent'] += 1
        stats['pre_bytes_empty' if not pre else 'pre_bytes_present'] += 1
    print(f'jobs={len(js)}')
    for k in sorted(stats):
        print(f'{k}={stats[k]}')
    if dist:
        print('gap_histogram=' + ','.join(f'{g}:{n}' for g, n in sorted(dist.items())[:20]))
    return 0 if stats['matched'] else 1


def compare(a, b):
    ja, jb = jobs(a), jobs(b)
    if not ja or not jb:
        print('no captures'); return 1
    h = lambda j: hashlib.blake2b(j['tiles'], digest_size=8).digest()
    n = min(len(ja), len(jb))
    aligned = sum(1 for x, y in zip(ja, jb) if h(x) == h(y))
    first_div = next((i for i, (x, y) in enumerate(zip(ja, jb)) if h(x) != h(y)), None)
    ent = sum(1 for x, y in zip(ja, jb) if h(x) == h(y) and x['ent'] != y['ent'])
    out = sum(1 for x, y in zip(ja, jb) if h(x) == h(y) and x['out'] != y['out'])
    print(f'jobs_a={len(ja)} jobs_b={len(jb)} compared={n} same_tiles={aligned} first_tile_divergence={first_div}')
    print(f'entropy_differs={ent} output_differs={out}')
    return 0


def main(argv):
    if len(argv) >= 4 and argv[1] == 'report':
        return report(argv[2], argv[3:])
    if len(argv) == 4 and argv[1] == 'compare':
        return compare(argv[2], argv[3])
    print('\n'.join(__doc__.strip().splitlines()[3:5]), file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
