# Intermediate zoom-out Goldfish: design_draft.md rev 2 (second pass)

Reviewer: fresh subagent, persona `agents/zoom_out.md` + `zoom-out-standard.md` + `implementation-readiness-standard.md`, no conversation context. Checked against the repo, the `perf/vp9-arm64` branch and PR #86.

## 1. Earlier Critical / High findings

| Finding | Status in rev 2 |
|---|---|
| Zoom-out H1 kernel-level option | Resolved (option K; but see H2 below) |
| Zoom-out H2 bench alone with numeric bar | Partly: bar uses the wrong unit/statistic (H1 below) |
| Zoom-out H3 SurfaceCache staleness | Resolved as a requirement |
| Comprehension C1, C2, H3, H4 | Resolved |
| Comprehension H1 B and intra-only | Partly: B's handling of intra-only not stated |
| Comprehension H2 | Moot (A dropped) |
| Critic D1 sign bias | Resolved (A dropped; B pins `error_resilient_mode` 0) |
| Critic D2 key-frame slots | Resolved |
| Critic D3 speed gate | Partly (H1 below) |
| Critic D4, D5, D6 | Resolved (requirements / B mechanism / eligibility) |
| Readiness C1 zero-decision | Resolved (Phase 2 deferred to a per-option plan) |
| Readiness H1, H2 | Resolved (fallback requirements) |
| Readiness H3, H4, H5 | Deferred to Phase 2 (acknowledged) |
| Readiness H6 corpus generator | Open (M4 below) |

## 2. Findings on rev 2

No Critical.

### High
- **H1.** The 1a bar compares a per-packet timing with a per-decode budget, and uses p95 against a budget built from mean throughput. `vpxbench` decodes one IVF packet per call (a superframe packet = hidden + shown frame), so its unit is per shown frame (budget ≈ 18.6 ms). Playback speed follows the mean. The bar also depends on 1e. Fix: "per IVF packet, mean ≤ 18.6 ms" (p95 for information); decide only after 1e confirms the 15 ms is fixed.
- **H2.** Option K overlaps PR #86. #86 already put libvpx `vpx_convolve8` NEON's int16 algorithm and a vector loop filter into C#, so swapping those for libvpx gains only codegen; whole-frame libvpx's real wins (row-MT, faster entropy decoding/traversal) are out of K's reach. libvpx DSP functions are reached via generated `*_rtcd.h` and an init call, not a public layer, and the Android recipe keeps only `src/vpx` headers (`android/codecs/vpx-build.sh:18`). Fix: restate K's scope (inverse transforms, intra prediction); 1b compares per kernel C# AdvSimd (post-#86) vs libvpx NEON, on the Mac first (`services/vpx/build.sh:46` has VP9 + NEON); note K uses libvpx internal symbols.
- **H3.** Phase 1 assumes a local IVF nothing in the repo produces. P4G movies are CRI USM; no USM demuxer in `tools/`; libvpx built with `--disable-webm-io`; FFmpeg excluded. Fix: add a USM→IVF extractor with an output contract.

### Medium
- **M1.** 1a changes the shipped Android libvpx recipe; give the exact flags (`--disable-vp9-encoder --enable-vp9-decoder`, as Apple) and whether it ships.
- **M2.** `vpxbench` should link `services/vpx/s2a_vp9.c` (row-MT on, `s2a_vp9.c:10-27`); there is no C block-linear writer, so take the copy cost from the C# profile; name the build script.
- **M3.** Decision table lacks "nothing fits"; move the one game-mode (X2) measurement into Phase 1.
- **M4.** Goal 2 "all 116,534 frames" cannot be verified from in-game captures alone; limit to the captured corpus or name an input generator as a Phase 2 deliverable.
- **M5.** Phase 1 depends on open PR #86; branch from `perf/vp9-arm64` or wait for merge.
- **M6.** 1e cites an off-CPU method #86 does not document; name the tool (`simpleperf record --trace-offcpu` or a Perfetto sched trace).

### Low
- L1 reuse `S2ARecord.Hash` (FNV-1a-64, `compat/tree/src/Ryujinx.Common/S2ARecord.cs:31-41`) and the dump-dir pattern of `S2A_MACRO_DUMP` (`S2AMacroAot.cs`).
- L2 `SetInBufBaseOffset` is in 256-byte units; a padded layout would leave a gap before the tile data; revisit option 0's "ends exactly" rule from the results.
- L3 also capture `Vp9SetSegmentReadBufOffset`, `Vp9SetSegmentWriteBufOffset`, `Vp9SetTileSizeBufOffset` (`NvdecRegisters.cs`).
- L4 mark option B's pinned header values and LRU policy as feasibility notes, not settled.
- L5 K's bit-exactness holds for in-range coefficients; state the limit for untrusted input.
- L6 check gate/test registration for new tools (`tools/contrib_tools.tsv`, `tools/gate.sh`).

## 3. Phase 1 ready without impactful decisions?

No. Open items: (1) IVF source; (2) gate unit/statistic/ordering; (3) exact configure flags and whether they ship, vpxbench build; (4) vpxbench via `s2a_vp9.c`; (5) copy timing; (6) 1b file, harness, function list, headers, comparison baseline; (7) matching captured jobs to IVF frames; (8) what the output hashes cover; (9) how "stops at unmapped memory" is implemented (`DeviceMemoryManager.IsMapped`, per page); (10) 1d comparison baseline, and an MV-buffer test; (11) 1e tool; (12) base branch and patch order vs open PR #86.

## Verdict

Rev 2 closes every earlier Critical and most Highs and is honestly framed as a feature spec with Phase 2 deferred. Remaining problems are in the evidence phase: wrong unit/statistic for the go/no-go bar, no producer for the IVF input, option K presented without noting that #86 already ported the same NEON algorithms, no method for 1b, and an off-CPU method and base branch cited but absent. Fix H1-H3 and the twelve items (mostly one or two sentences each plus one small extractor). Then Phase 1 meets the Zero-Decision Rule and can be approved; no further architectural rework is needed.
