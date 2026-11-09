# Phase 1 results: native VP9 decoding for Nvdec

* **Design:** [design_draft.md](./design_draft.md) (rev 3, Phase 1 approved 2026-11-09)
* **Device:** ROG Phone 6D (ASUS_AI2203, Dimensity 9000+), Android 14, game mode off
* **Follow-up:** the Phase 1 tools (vpxbench, capture hook, `tools/vp9_capture_check.py`) are on the Phase 1
  branch and land with the native VP9 PRs (#231 has vpxbench). The picture mismatch against C# noted in later
  work was a C# loop-filter bug, fixed in #226.
* **Stream:** P4G opening, movie 36 from `data_movie.cpk` (`tools/usm_to_ivf.py`), 2817 IVF packets, 1920x1080, 29.97 fps

## 1a. libvpx speed

Tool: `android/codecs/vpxbench/` (`build.sh`, `vpxbench.c`). libvpx 1.15.0, the same pinned source as
`services/vpx/build.sh`. Configured as a VP9 decoder in the bench's own build: `--disable-vp8
--disable-vp9-encoder --enable-vp9-decoder`, NEON on. Each packet is decoded with `services/vpx/s2a_vp9.c`, the
production wrapper, which turns on row-MT when threads > 1. The shipped Android recipe was **not** changed: it
stays VP8-only until Phase 2. Runs used `taskset 70`, which pins to cpus 4-6, the three A710 cores the game also
gets.

Per IVF packet, decode only, ms:

| Threads | Load | Packets | mean | p50 | p95 | max |
|---|---|---|---|---|---|---|
| 1 | idle | 2817 | 9.59 | 8.66 | 26.78 | 44.00 |
| 2 | idle | 2817 | 5.37 | 4.55 | 14.60 | 25.56 |
| 3 | idle | 2817 | 3.63 | 3.17 | 10.13 | 24.23 |
| 2 | game running | 900 | 4.73 | 4.31 | 12.92 | 22.91 |
| 1 | game running | 900 | 8.57 | 8.10 | 23.85 | 43.13 |
| 3 | game running | 900 | 3.94 | 3.21 | 10.62 | 55.76 |
| 2 | game running | 2817 | 5.09 | 4.41 | 14.11 | 59.22 |
| 2 | game running | 2817 | 5.18 | 4.45 | 14.59 | 30.89 |

"Game running" is harsher than the title screen the plan named. During every loaded run the game was playing
this same opening movie, so its own C# decoder was busy on the same cores. There were no decode errors, and
every packet produced a frame.

**Copy cost:** in the post-#86 profile, `SurfaceWriter` (block-linear write of the output) is about 2.3% of the
decoder-related samples. That is under 1 ms per frame at the measured rates. Assumed: ≤ 1 ms.

**Pass bar** (mean decode + copy per IVF packet ≤ 18.6 ms with 2 threads under load): **passes**, ≈ 6.2 ms.
The bar holds unless the fixed per-frame overhead found by 1e exceeds about 27 ms of the 33.4 ms frame interval.
That is impossible, since the measured idle time is about 15 ms. So 1e cannot change this decision (see 1e).

## 1b. Kernel estimate for option K

Not benchmarked; the upper bound already settles it. Fitting the budget needs the C# decode path to fall from
about 33 ms busy to 18.6 ms per shown frame, a 44% cut. K's scope after #86 is inverse transforms and intra
prediction. In the post-#86 phone profile these are about 3% of the decoder-thread samples (`InverseTransform*`,
`Idct*`; intra prediction under 1%). Even if all unresolved samples (about 8%) belonged to them, the total stays
under 11%. Making those kernels cost nothing would leave the C# path at about 30 ms. This profile's symbol
mapping is approximate: a few non-decoder symbols appear on decoder threads, so the per-class shares are rounded
generously. **K cannot meet Goal 1.**

## 1c. NVDEC inputs captured on the phone

Tools: the debug-only hook `compat/tree/src/Ryujinx.Graphics.Nvdec/S2AVp9Capture.cs` (patch
`20261109-0146-nvdec-vp9-capture`) and `tools/vp9_capture_check.py`. The run was a debug build cold-started into
the opening, with 1033 jobs captured over about 85 s.

The movie the game plays at boot is **movie 37**, not 36. They are two encodes of the same opening with the
same packet count; 36 is the one PR #86's speed readings matched against. libvpx timings on 37 match 36:
1 thread 9.41 ms mean, 2 threads 5.41 ms mean, idle.

| Question | Result |
|---|---|
| Captured jobs matched to an IVF frame by their tile data | 1033 / 1033 (all in movie 37) |
| `BitstreamSize` ends exactly at the frame's end (no superframe index inside) | 1033 / 1033 |
| Original frame header right before the tile data in guest memory | **0 / 1033** |
| Where the tile data sits | always GPU VA `0x1000`, the same buffer for every job; the page below it is unmapped |
| MV read buffer = previous job's MV write buffer | 1032 / 1032 |

The guest driver copies only the tile data into one fixed buffer at the bottom of the address space. The
frame's headers are not in front of it, so **option 0 is not possible** for P4G.

## 1d. Counts and motion vectors read by the guest

Three cold-start runs of the opening were made on the same debug build: one as a baseline, one zeroing the
counts buffer after every job, and one zeroing the MV write buffer after every job. They were compared job by
job, with tile data checked equal across runs.

| Perturbation | Jobs compared | `EntropyProbs` differ | C# output differs |
|---|---|---|---|
| counts zeroed | 889 | 0 | 0 |
| MVs zeroed | 809 | 0 | 710 |

- **Counts:** P4G's driver does not read them back, as expected for a stream with `frame_parallel_decoding_mode`
  set to 1.
- **MVs:** the guest feeds the MV write buffer back as the next job's MV read buffer, and the picture depends on
  it. Zeroing it visibly corrupts the video.
- **Counts for an adapting stream** (GRID Autosport) were not tested. That game is not on this phone.

## 1e. Idle time

Not run. Its only role was to confirm the 18.6 ms budget. 1a's ≈ 6.2 ms fits unless the fixed per-frame
overhead exceeds about 27 ms. All of the measured idle time together is about 15 ms, so no outcome of 1e changes
the decision.

## 1f. Game mode

Not run. It feeds only the "nothing fits" row of the decision table, and 1a shows libvpx fits in default mode.
Turning game mode on is a device setting, left to the owner.

## Readings

| Item | Recorded |
|---|---|
| libvpx per-packet decode, 2 threads under load | mean 5.1–5.2 ms, p95 14.1–14.6 ms |
| copy ms | ≤ 1 (from profile) |
| K estimate per shown frame | ≥ 30 ms of C# work remains; does not fit |
| Header present before tile data | 0 of 1033 jobs; tile data is alone at GPU VA 0x1000 |
| Superframe index inside `BitstreamSize` | no (1033 of 1033) |
| MV read buffer = previous MV write buffer | 1032 of 1032 |
| Counts / MVs read by guest (P4G) | counts: no; MVs: yes (710 of 809 outputs change) |
| Idle time by class | not run (not decision-relevant) |
| Game-mode playback speed | not run |
| Capture corpus size | 1033 jobs, movie 37 (local only) |

## What the results select

| Decision-table row | Result |
|---|---|
| libvpx whole-frame mean > 18.6 ms | no: about 5.2 ms with 2 threads under load |
| K fits the budget | no: at least 30 ms of C# work remains |
| headers found before tile data on ≥ 99.9% of jobs | no: 0% |
| libvpx fits and K does not | **yes → option B** |
| guest ignores counts and MVs | counts yes (P4G only); MVs no |

**Option B is the only viable option.** For P4G, B's patch must import the guest's MV read buffer and export MVs
in the guest's layout. libvpx's own previous-frame MVs would carry the same values, since the read buffer equals
the previous write buffer on every job, but the guest still reads the write buffer. Counts export is not needed
for P4G but is still needed for adapting streams. The design keeps it so B stays correct for every stream.
Next: `design_phase2_B.md`, after the owner confirms B.
