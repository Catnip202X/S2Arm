# Design: native VP9 decoding for Nvdec

* **Author:** Catnip202X
* **Date:** 2026-11-08 (rev 3, after two Goldfish review rounds)
* **Status:** Phase 1 (evidence) approved by the owner on 2026-11-09; Phase 2 is a decision, not a plan yet.
* **Brief:** [concepts_brief.md](./concepts_brief.md) · **Reviews:** [reviews/](./reviews/)
* **Issue:** jhaizhou-ops/S2Arm#44 · **Base:** PR #86 merged first (its ARM64 chroma copy and vector paths are assumed)

## What changed since rev 1

Rev 3 (second review round) fixed Phase 1: the speed gate uses mean time per IVF packet; option K is
re-scoped after PR #86; and the IVF input tool, capture layout, perturbation test, idle-time method and base
branch are now specified.

Rev 2 (first review round):


- **Option A (rebuilt headers with stock libvpx) is dropped.** It relied on `error_resilient_mode` to switch
  off previous-frame motion vectors. In libvpx that mode also zeroes `ref_frame_sign_bias`
  (`vp9_setup_past_independence`, `vp9/common/vp9_entropymode.c:466` in 1.15.0). In P4G every one of the
  10,674 frames after a hidden alt-ref has sign bias (0,0,1), so A would corrupt them all (critic D1, checked
  against the local files).
- **New option K:** keep the C# decoder and call libvpx's NEON kernels from it (zoom-out H1).
- **The speed budget is about 18 ms per shown frame, not 33 ms** (critic D3).
- **Native-to-C# fallback is not exact in today's code** (surface cache, segment map). This is now a stated
  requirement for options B and 0.
- This document is now a feature spec plus the Phase 1 plan. The Phase 2 implementation plan is written for
  the chosen option after Phase 1 (all four reviewers).

## The Problem

On Android, Persona 4 Golden's 2D anime cutscenes (1080p VP9, 29.97 fps) play in slow motion: 0.53x real
speed before PR #86 and 0.70x after it, on a ROG Phone 6D. The game shows a frame only after NVDEC has decoded
it, so playback speed is bounded by the per-frame time of the decode loop. The picture falls behind the sound.

Speed is measured by screen-recording 60 s of the opening and matching each recorded frame to its source
frame in the game's own file. Speed = source frames advanced / wall seconds, over forward steps of 1-10
frames. Run-to-run spread in PR #86 was ±0.03x (`docs/坑记录.md`, entry for #44).

## Overview

Switch games send video frames to the console's NVDEC block, and S2Arm emulates NVDEC with Ryujinx's VP9
decoder (C#). NVDEC does not receive a normal VP9 frame:
- The guest driver has already parsed the frame headers.
- It passes only the tile data, a parameter block (`PictureInfo`) and the final probability tables
  (`EntropyProbs`).
- It expects the picture, motion vectors and symbol counts back.

The C# decoder is built for exactly that interface. libvpx (Google's VP9 decoder, C with NEON, already built
from source at 1.15.0 for VP8 and for the Switch 2 `nn::codec` path; Android builds it without VP9 today) is
built for whole frames. This design decides how much of libvpx to use, and how, without changing anything the
guest sees.

## Glossary

| Term | Meaning |
|---|---|
| guest | the game plus Nintendo's driver code, running on S2Arm |
| NVDEC job | one decode request from the guest: one VP9 frame (a superframe's frames arrive as separate jobs) |
| superframe | several VP9 frames packed into one packet with a trailing index; P4G packs a hidden alt-ref frame with a shown frame |
| hidden / alt-ref frame | a frame decoded only to be used as a reference, never shown (`show_frame` 0) |
| frame context | one of 4 saved sets of probability tables in a VP9 decoder; "context 0" is the first |
| frame-parallel stream | `frame_parallel_decoding_mode` 1: no backward adaptation, so counts do not affect later frames |
| backward adaptation / counts | after a frame, the decoder updates probabilities from symbol counts; NVDEC returns the counts and the guest driver does the update |
| `use_prev_frame_mvs` | whether motion-vector prediction may use the previous frame's vectors |
| sign bias | per-reference flag that flips motion-vector candidates from references in the other temporal direction |
| block-linear, NV12, I420 | NVIDIA's tiled surface layout; luma plane + interleaved chroma; three planes |
| VIC | the Switch's video image compositor, which converts the decoded surface for display |
| syncpoint | the counter the guest waits on to know a job is done |
| `【红】` | a red log line that reports an unsupported case instead of returning a fake success ([AGENTS.md](../../AGENTS.md), "未实现的服务一律报红") |

## Goals

1. P4G's opening plays at **≥ 0.97x median of 3 runs** (1.0x within the ±0.03x spread) on the ROG Phone 6D in
   its default mode (X2 core not available), plugged in, 36-38 °C.
2. Guest memory after each natively decoded frame is byte-identical to what the C# decoder writes: the
   picture always, plus the motion-vector and counts buffers unless Phase 1 proves the guest does not read
   them. Checked over every captured job: Phase 1 captures of P4G's opening and of every cutscene reached in a
   save, stated by job count in `phase1_results.md`, plus the adapting-stream case in Goal 4. Covering all
   116,534 frames without playing them would need a guest-driver emulator; that is not in scope.
3. Any frame the native path cannot reproduce exactly is decoded by the C# decoder, or reported with
   `【红】`. Never silently wrong.
4. GRID Autosport (an adapting stream: its driver adapts from the counts NVDEC returns, see
   `tests/vp9_coef_counts`) keeps decoding correctly.
5. One implementation for Android and Apple ARM64.

## Non-Goals

- VP9 profiles 1-3 and high bit depth; hardware decoders (MediaCodec, VideoToolbox); x86 hosts.
- Changing guest-visible timing, syncpoints or VIC.
- Removing the C# decoder. It stays as fallback and reference.

## Facts

- **Per-job inputs** (`compat/tree/src/Ryujinx.Graphics.Nvdec/Vp9Decoder.cs`, `Types/Vp9/PictureInfo.cs`):
  - tile data at `SetInBufBaseOffset` (`BitstreamSize` bytes);
  - `PictureInfo`: sizes, quantiser, loop filter, segmentation, tiles, sign bias, filter, tx mode, reference
    mode, and flags `IsKeyFrame`, `LastFrameIsKeyFrame`, `FrameSizeChanged`, `ErrorResilientMode`,
    `LastShowFrame`, `IntraOnly`;
  - `EntropyProbs`: final probability tables, including key-frame tables and segmentation probabilities;
  - luma and chroma offsets of Last, Golden, Alt and the output surface;
  - motion-vector read and write buffers;
  - the counts buffer.
- **Not passed:** the current frame's `show_frame`, `refresh_frame_flags`, `ref_frame_idx`,
  `frame_context_idx`, `refresh_frame_context`, `frame_parallel_decoding_mode`.
- **P4G** (38 movies, 116,534 frames, parsed from the local files):
  - `frame_parallel_decoding_mode` is 1 on every frame;
  - 4 tile columns;
  - 1,957 key frames;
  - 10,674 hidden alt-ref frames, each in a superframe;
  - sign bias (0,0,1) on 89,387 frames, including every frame after a hidden one;
  - no segmentation, error-resilient, intra-only or `show_existing_frame`.
- **Measured after PR #86:**
  - the Nvdec thread is busy 69% of wall time at about 21 source frames/s, which is about 33 ms busy plus
    15 ms idle per shown frame;
  - there are about 1.10 decodes per shown frame;
  - the app's cpuset is cpus 0-6 (three A710 big cores; the X2 is excluded);
  - the process averages about 2.8 cores;
  - remaining hot code is VP9 DSP (prediction, loop filter) and loop-filter thread sync.
- **The C# fallback is not stateless today:**
  - `SurfaceCache.Get` reuses a cached picture on an exact offset match without re-reading guest memory
    (`Image/SurfaceCache.cs:43-58`);
  - the C# decoder keeps `_lastSegmentMap` between frames (`Nvdec.Vp9/Decoder.cs:14-16`);
  - one static C# decoder serves every context (`Nvdec/Vp9Decoder.cs:17`).
- **Unknown until Phase 1:**
  - whether the original headers sit in guest memory before the tile data;
  - whether `BitstreamSize` includes a superframe index;
  - whether the guest reads the counts and MV buffers for frame-parallel streams;
  - whether the guest's MV read buffer is always the previous job's write buffer;
  - what the Nvdec thread's 15 ms of idle time per frame is.

## Speed budget

At 29.97 fps a shown frame has 33.4 ms. If the 15 ms of non-decode time per frame is fixed serial work, the
decode path (decode + surface copy + MV/count write-back) must fit in about **18.6 ms per shown frame**. That
is the same as **per IVF packet**, because a P4G superframe packet holds the hidden frame and the shown frame
it precedes (about 1.10 decodes per shown frame, so about 16.9 ms per individual decode).

Playback speed follows mean throughput, so the gate uses the **mean**; p95 is recorded for information. The
gate is applied only after Phase 1 step 1e confirms the 15 ms is fixed. If it is not, `phase1_results.md`
recomputes the budget first.

## Options (owner decides; not ranked)

Each option lists what it writes to guest memory, so Goal 2 can be checked per option.

### K: libvpx kernels inside the C# decoder

- **Mechanism:** keep `Ryujinx.Graphics.Nvdec.Vp9.Decoder` and all its state. Replace DSP calls that are
  still scalar in C# with libvpx's NEON functions through `s2a_vpx`, at a granularity coarse enough that
  P/Invoke cost does not dominate.
  - PR #86 already ported libvpx's int16 NEON convolve algorithm and a vector loop filter to C# AdvSimd, so
    swapping those for libvpx gains only code generation.
  - K's realistic scope is what PR #86 left scalar: inverse transforms and intra prediction.
  Phase 1 (1b) measures how much time those take.
- **Writes back:** picture, MVs and counts, all from the C# decoder as today.
- **Trade-offs:**
  - No header rebuilding, no reference slots and no fallback problem.
  - The libvpx DSP functions are internal: they are reached through generated `vpx_dsp_rtcd.h` /
    `vp9_rtcd.h` and an `*_rtcd()` init call. So K also depends on libvpx internals, though on fewer than B.
  - Bit-exact for in-range data: libvpx tests its NEON kernels equal to its C reference, which the C# port
    mirrors. For malformed coefficients the two may differ, and guest bytes are untrusted.
  - The ceiling is lower: whole-frame libvpx's main wins (row-level threads, faster entropy decoding and
    block traversal) are out of K's reach.
- **Fails when:** P/Invoke cost per call exceeds the kernel savings, or the remaining C# work alone exceeds
  the budget.

### B: whole frames into libvpx, rebuilt headers, pinned libvpx patch

- **Mechanism:** write an uncompressed header and a compressed header from `PictureInfo`, append the tile
  data, and decode with libvpx. A patch to libvpx 1.15.0 adds one entry point that, for the next frame:
  - installs the guest's `EntropyProbs` after the compressed header is read;
  - skips libvpx's own backward adaptation;
  - imports the guest's MV read buffer and sets `use_prev_frame_mvs` and `last_show_frame` from the guest's
    flags;
  - exports counts and MVs in the guest's layout.
  The header values are pinned: `error_resilient_mode` 0 (never used to steer MVs),
  `frame_parallel_decoding_mode` 0 (so libvpx counts), `refresh_frame_context` 0, `frame_context_idx` 0,
  every loop-filter delta sent each frame. The compressed header carries tx mode (unless lossless) and
  reference mode with all update flags off. Reference slots: on a key frame all 8 slots take the output
  offset. Otherwise `ref_frame_idx` names the slots holding the guest's Last, Golden and Alt offsets, and the
  output refreshes the least recently used slot not named in this frame's `ref_frame_idx`. A missing reference
  is imported from guest memory with `VP8_SET_REFERENCE`. Hidden frames are fetched with
  `VP9_GET_REFERENCE`.
- **Writes back:** picture, counts and MVs from libvpx, converted to the guest layout.
- **Eligibility checks per frame:**
  - the guest's key-frame tables (`KfYModeProb`, `KfUvModeProb`, `KfPartitionProb`) equal libvpx's
    constants;
  - 8-bit;
  - size within libvpx's limits.
  Anything else goes to C#.
- **Trade-offs:** works for adapting and frame-parallel streams. Costs a patch against libvpx internals
  (`VP9_COMMON`, `FRAME_CONTEXT`, `FRAME_COUNTS`, `MV_REF`), a field mapping from Ryujinx's
  `Vp9EntropyProbs` (a superset of libvpx's `FRAME_CONTEXT`), and the fallback requirements below.
- **Fails when:** a libvpx upgrade moves those internals (caught by the pin and the replay test); a guest
  probability table has no place in libvpx (the checks route it to C#).

### 0: original frames from guest memory (only if Phase 1 finds them)

- **Mechanism:** if the bytes before `SetInBufBaseOffset` hold the frame's original headers, give libvpx the
  original frame.
  - **Header search:** scan backward for candidates whose frame marker is 2, whose uncompressed header
    parses, whose header length plus `header_size_in_bytes` ends exactly at `SetInBufBaseOffset`, and whose
    fields equal `PictureInfo`. Limit the scan to 64 KiB (the compressed-header size field is 16 bits), and
    stop at unmapped memory.
  - **Hidden frames:** fetch them with `VP9_GET_REFERENCE` on the refreshed slot.
  - **Drift check:** every frame, compare libvpx's parsed header and slot contents with `PictureInfo` and the
    guest's reference offsets. On a mismatch, use C# until the next key frame.
- **Writes back:** the picture. MVs and counts are written only if Phase 1 shows the guest reads them, which
  would need B's export patch. Frames whose original header has `frame_parallel_decoding_mode` 0 go to C#
  (GRID Autosport).
- **Trade-offs:** stock libvpx, no header writer, libvpx manages its own state. It depends on a property of
  Nintendo's driver that is not documented.
- **Fails when:** headers are not there for some title or driver version; fallback to C# is exact only at key
  frames.

### What Phase 1 results select

| Phase 1 result | Leads to |
|---|---|
| libvpx whole-frame mean (decode + copy) > 18.6 ms per IVF packet under load with 2 threads | B and 0 cannot meet Goal 1; K or continued C# work only |
| K's estimate fits 18.6 ms per shown frame | K is enough; B and 0 not needed |
| headers found before the tile data on ≥ 99.9% of P4G jobs, and libvpx fits the budget | 0 is viable for frame-parallel streams |
| libvpx fits the budget, and K does not | B |
| guest provably ignores counts and MVs for frame-parallel streams | 0 needs no export patch |
| nothing fits | Goal 1 is not reachable in default mode; the owner chooses between game mode (X2 core, measured in 1f), accepting the current speed, or more C# work |

The option B mechanism above (pinned header values, slot policy) is a feasibility note, not a settled plan.
If B is chosen, its Phase 2 plan restates it field by field.

The owner picks among the viable options. The Phase 2 plan is then written for that option only.

## Fallback requirements (B and 0)

1. Every native write of a picture to guest offset X invalidates any `SurfaceCache` entry with luma offset X,
   so a later C# decode re-reads guest memory (new `SurfaceCache.Invalidate`, via a compat patch).
2. The C# decoder becomes per-context (held in `NvdecDecoderContext`), so its segment-map state belongs to one
   stream.
3. A switch from native to C# at a non-key frame is allowed only when segmentation is off, or the frame
   rewrites the whole map (`UpdateMap` 1 and `TemporalUpdate` 0).
4. A native decode error stops native decoding for that context until the next key frame, with one `【红】`.

## Risks & Mitigations

| Risk | Option | Detection | Mitigation |
|---|---|---|---|
| Guest-controlled bytes reach native C/NEON code in-process | K, B, 0 | fuzzing pass on mutated captures (Phase 2) | libvpx bounds checks; size caps before decode; follow libvpx security fixes for the pinned version |
| libvpx upgrade breaks the patch | B | pin + replay test | patch hash in the libvpx build key; one named owner for the patch |
| Header-search false match | 0 | per-frame cross-check against `PictureInfo` | refuse until the next key frame |
| Native decode too slow under the game's real load | all | Phase 1 bench under load | the decision table above |
| C# port and libvpx disagree on some stream | K, B, 0 | three-way check: C#, native, `vpxdec --md5` on the local file | libvpx output is the reference; a C# divergence is a C# bug, filed separately |

## Alternatives Considered

- **A: rebuilt headers with stock libvpx.** Rejected. See "What changed since rev 1".
- **One-frame lookahead** (yuzu's way of learning refresh flags and `show_frame`). Rejected: the guest reads
  the output surface when the job completes.
- **MediaCodec / VideoToolbox.** Rejected: whole frames needed, no output for hidden frames, latency outside
  our control, iOS VP9 support not established.
- **FFmpeg's VP9 decoder.** Rejected: S2Arm does not ship FFmpeg (`android/codecs/README.md`: "no FFmpeg or
  MediaCodec fallback").
- **More C# work** (tile-row threads, wider vectors). Not rejected; independent of this document.
- **Phone game mode** (opens the X2 core). A user setting; one measurement is worth having, but Goal 1 is the
  default mode.

## Detailed Implementation: Phase 1 only (evidence)

**Base:** branch from `perf/vp9-arm64` (PR #86), or from `main` once #86 is merged. 1c's compat patch goes
after #86's two patches in `compat/patches/series`. Paths are relative to the repository root. `compat/tree/`
changes go through `compat/patch_new.sh` ([compat/README.md](../../compat/README.md)). The only shipped change is
1a's libvpx configure flags. Everything else is a tool, a debug-only path, or a document.

### Input: `tools/usm_to_ivf.py` (written)

P4G's movies are CRI USM files inside `data_movie.cpk`. This tool writes each movie's VP9 stream as IVF, one
packet per USM video chunk, with superframes intact (contract in the file's docstring; standard library only).
It was checked on the opening (movie 36): its output is byte-identical to the stream used for PR #86's
readings, and `ffprobe` reads it as VP9 1920x1080, 2817 packets. Game files and IVF output stay on the
developer's machine.

### 1a. libvpx speed bench (lands first)

| File | Change and reason |
|---|---|
| `android/codecs/vpx-build.sh` | Replace `--disable-vp9` with `--disable-vp9-encoder --enable-vp9-decoder`, matching `services/vpx/build.sh:46`. Pass both `s2a_vp8.c` and `s2a_vp9.c` to the shim build, and fix the line-1 comment. This ships, but nothing calls the VP9 objects until Phase 2 registers a backend. 1a records the size of `libs2arm_android.so` before and after. If the unused VP9 decoder is linked in anyway (the shim archive is merged with `llvm-ar qcL`, `cache.sh:80`), the bench builds its own archive and the shipped recipe stays VP8-only until Phase 2. |
| `android/codecs/cache.sh` | `codec_shim` compiles several sources and hashes all of them into its key (today it takes one, :72-82). |
| `android/core-link/link_symbols.py`, `android/docker/cache-identity.py`, `android/docker/test_gate_apk.py`, `android/doctor/test_doctor.py` | Add `s2a_vp9.c` wherever `s2a_vp8.c` is listed (:845, :27, :448, :38), so the identity and symbol checks stay true. |
| `android/codecs/vpxbench/vpxbench.c` (new) | Reads an IVF and decodes each packet with `s2a_vp9_open(threads)` / `s2a_vp9_decode` from `services/vpx/s2a_vp9.c` (row-MT on when threads > 1, the production setting). Prints per-packet decode ms: mean, p50, p95, max. |
| `android/codecs/vpxbench/build.sh` (new) | Sources `android/codecs/cache.sh`, builds the cached libvpx (`vpx-build.sh`), and links `vpxbench.c` + `s2a_vp9.c` + `libvpx.a` with the NDK clang for arm64-v8a/API 29. |
| `android/codecs/vpxbench/run.sh` (new) | `adb push`es the binary and one IVF to `/data/local/tmp`. Runs `taskset 70 vpxbench <ivf> <threads>` (cpus 4-6, the A710s) for 1, 2 and 3 threads: twice with the phone idle, twice while the game sits on its title screen. The IVF path is an argument. |

**Copy cost:** there is no C block-linear writer, so it is not re-implemented. Its per-frame cost is taken from
the post-#86 simpleperf profile of `SurfaceWriter.Write` and added to the decode mean.

**Pass bar:** mean (decode + copy) per IVF packet ≤ 18.6 ms with 2 threads while the game runs, on the
opening (movie 36). It is applied after 1e.

### 1b. Kernel estimate for option K (Mac first)

| File | Change and reason |
|---|---|
| `tests/vp9_kernel_bench/` + `tests/vp9_kernel_bench.sh` (new, not a gate) | For each kernel class still scalar after #86 (inverse transforms 4x4-32x32 by type, intra predictors by size), and for convolve and loop filter as a control, time the C# function against the libvpx function. Uses the same random blocks, a fixed seed, and 10^5 calls per size. Runs on the Mac against `services/vpx/build.sh`'s libvpx. That recipe has VP9 and NEON; its `*_rtcd.h` headers come from the libvpx build directory. |
| `docs/设计_原生VP9/phase1_results.md` (new) | Share of C# decode time per kernel class from the phone profile (simpleperf, as in #86), times the measured speedup per class = K's estimated per-frame saving. |

### 1c. Capture of NVDEC inputs (debug builds only)

| File | Change and reason |
|---|---|
| `compat/tree/src/Ryujinx.Graphics.Nvdec/Vp9Decoder.cs` | When `S2A_NVDEC_VP9_CAPTURE=<dir>` is set, write one `frame_%06d.bin` per job, following the dump-directory pattern of `S2A_MACRO_DUMP` (`Ryujinx.Graphics.Gpu/Engine/MME/S2AMacroAot.cs`). On Android, `S2A_*` variables reach the process only in debuggable builds (`DebugEnvironment.java`); files go to the app's private directory and are pulled with `adb`. |
| `tools/vp9_capture_check.py` (new) | Matches each captured job to its IVF frame by the FNV-1a-64 hash of the tile data, searched against every sub-frame of every packet in the movie. It then reports per job: whether and how far before the tile data the original header sits; whether `BitstreamSize` includes a superframe index; whether the MV read offset equals the previous job's MV write offset. |

**Record layout** (little-endian):
- header: magic `S2V9`, version 1, job index, and the byte size of each section below;
- `PictureInfo` (0x100 bytes) and `EntropyProbs`;
- the NVDEC register offsets used by VP9:
  - picture luma and chroma for Last, Golden, Alt and output;
  - `Vp9SetColMvReadBufOffset`, `Vp9SetColMvWriteBufOffset`, `Vp9SetCtxCounterBufOffset`;
  - `Vp9SetSegmentReadBufOffset`, `Vp9SetSegmentWriteBufOffset`, `Vp9SetTileSizeBufOffset`;
- the tile data;
- the bytes before the tile data, up to 64 KiB, preceded by their length. The capture walks back page by page
  and stops at the first page where `DeviceMemoryManager.IsMapped` (`Ryujinx.Graphics.Device/DeviceMemoryManager.cs:308`)
  is false;
- `S2ARecord.Hash` (FNV-1a-64, `Ryujinx.Common/S2ARecord.cs:31-41`) of the guest memory the C# decoder wrote
  for this job: the output luma and chroma ranges as `SurfaceWriter` laid them out, the MV write buffer, and
  the counts buffer.

### 1d. Does the guest read counts and motion vectors for frame-parallel streams?

On a debug build, `S2A_NVDEC_VP9_PERTURB=counts|mvs` makes `Vp9Decoder.cs` write zeros to the counts buffer or
the MV write buffer after the C# decoder, for every job. For each setting, capture the same 60 s of the
opening from a cold start, as a baseline run and a perturbed run. Align the two by job index and tile-data
hash, then compare `EntropyProbs` and the output hashes. If nothing changes for P4G, the guest does not read
that buffer for this stream. Repeat with GRID Autosport, where the counts result is expected to change.

### 1e. Idle time

`simpleperf record --app <package> -e cpu-clock --trace-offcpu --call-graph fp` during the opening, the
method used to diagnose #86. Classify the Nvdec thread's off-CPU time by the managed frame it waits in:
- the NVDEC command queue (`BlockingCollection.TryTake`, i.e. waiting for the next job);
- tile and loop-filter workers (`Task` / `LfSync` waits);
- locks.

Sum each class per shown frame. Only the first class counts as fixed per-frame overhead in the budget.

### 1f. Game mode (one measurement)

With the phone's game mode on (X2 core available to the app), run PR #86's playback-speed measurement 3
times. This shows whether more big cores help at all.

### Phase 1 readings (go into `phase1_results.md`)

| Item | Recorded |
|---|---|
| libvpx per-packet mean/p50/p95 decode ms, 1/2/3 threads, idle and under load; copy ms | numbers |
| K estimate per shown frame | number |
| Header present before tile data | % of jobs, distance histogram |
| Superframe index inside `BitstreamSize` | yes/no |
| MV read buffer = previous MV write buffer | % of jobs |
| Counts read by guest; MVs read by guest | P4G and GRID: yes/no each |
| Idle time by class | ms per shown frame |
| Game-mode playback speed | 3 runs |
| Capture corpus size | jobs per movie |

## Phase 2 (after the owner's choice)

Written as `design_phase2_<option>.md` once `phase1_results.md` exists. It must include, for the chosen option:
- exact interfaces (the guest structs are internal to `Ryujinx.Graphics.Nvdec`, so the interface lives
  there or takes byte spans);
- the call-site change in `NvdecDevice.cs` (VP9 gets the decoder context, as VP8 already does);
- the Android multi-source shim (`android/codecs/cache.sh`) and its symbol, identity and probe lists
  (`android/core-link/link_symbols.py`, `android/docker/cache-identity.py`, `android/docker/test_gate_apk.py`,
  `android/doctor/test_doctor.py`, `android/cs/aot/AndroidCodecs.cs`);
- the factory contract tests (`android/cs/video/tests/VideoDecoderFactoryContractTests.cs`);
- for B, the field-by-field header table and the `FRAME_CONTEXT`/`FRAME_COUNTS`/`MV_REF` mapping tables;
- the replay harness and its corpus (captures from Phase 1, kept local, plus committed small fixtures with
  stated provenance and licence);
- buildable sub-steps.
