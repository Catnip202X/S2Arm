# Implementation-readiness Goldfish: design_draft.md (rev 1)

Reviewer: fresh subagent, persona `agents/implementation_readiness.md` + `implementation-readiness-standard.md` (elephant-goldfish-model), no conversation context.

**Can an engineer implement without impactful decisions?** Phase 1: mostly yes, with gaps. Phase 2 and 3: no. All listed paths exist (or their parent does); `services/vpx/patches/` is new, as expected.

## Critical
- **C1. Phase 2 has no chosen design (Zero-Decision Rule).** Every row is conditional on 0/A/B; no rule maps Phase 1 answers to an option, and A is admitted unsafe without an outside signal. Fix: approve Phase 1 alone; rewrite Phase 2 for one option with a decision table (headers present × libvpx ms/frame × guest reads counts); say whether A is still on the table given M1.

## High
- **H1. C# fallback serves stale reference pixels.** `SurfaceCache.Get` returns a cached surface on an exact offset match without re-reading guest memory (`Image/SurfaceCache.cs:44-58`; re-reads only at 78-81, 101-104). Fix: native writes invalidate matching cache entries (`SurfaceCache.Invalidate`) via a compat patch; add a route test.
- **H2. C# fallback is not exact for segmented streams.** The C# decoder keeps `_lastSegmentMap` across frames (`Nvdec.Vp9/Decoder.cs:14-16`, :181-219) and is one static instance for all contexts (`Vp9Decoder.cs:17`). Fix: allow native→C# switches only when segmentation is off or `UpdateMap=1 && TemporalUpdate=0`, or export/seed the map; make the C# decoder per-context.
- **H3. The interface cannot reference its types.** `PictureInfo`, `EntropyProbs`, `BackwardUpdates` are internal to `Ryujinx.Graphics.Nvdec`; `Ryujinx.Graphics.Video` references only `Ryujinx.Common`. Fix: byte-span signature, or put the interface in `Ryujinx.Graphics.Nvdec` (public); say who writes the surface.
- **H4. Call site missing.** `Vp9Decoder.Decode(rm, ref state)` gets no context (`NvdecDevice.cs:77`, vs VP8 at :74). Add `NvdecDevice.cs` and the `NvdecDecoderContext.Dispose` changes.
- **H5. Android build/link touchpoints missing.** `codec_shim` compiles one source (`android/codecs/cache.sh:72-82`, key :75); `vpx-build.sh:22` passes only `s2a_vp8.c`; `android/core-link/link_symbols.py:845`, `android/docker/cache-identity.py:27`, `android/docker/test_gate_apk.py:448`, `android/doctor/test_doctor.py:38` list only the VP8 shim; `android/cs/aot/AndroidCodecs.cs:20-23` probes VP8 only; `vpx-build.sh` line 1 says VP8-only.
- **H6. The test corpus generator is unspecified.** libvpx is built without encoder or tools, `testsrc2` is FFmpeg (excluded), and turning IVF into NVDEC inputs needs a guest-driver emulator (header parse + probability adaptation). Also no harness entry point or Mac library loading. Fix: committed fixtures with provenance/licence, a specified driver-emulator component, and the harness entry.

## Medium
- **M1.** A shipped title adapts: GRID Autosport's driver adapts from NVDEC counts (`tests/vp9_coef_counts/Program.cs:1-3`, `compat/改动清单.md:536`). Make it a required non-frame-parallel case; for option 0 route `frame_parallel_decoding_mode=0` to C# or produce counts.
- **M2.** Capture format unspecified: layout, record schema, hash, unmapped prefix handling (`MemoryExtensions.cs:13-23`), Android write location, variable delivery (`DebugEnvironment.java`, debug builds); include chroma, MV and counts offsets.
- **M3.** `vpxbench` underspecified: build recipe, IVF reader, CPU ids, run procedure, numeric threshold (e.g. p95 ≤ 25 ms).
- **M4.** Option B MV source ambiguous (libvpx's own vs guest `Vp9SetColMvReadBufOffset`, `Vp9Decoder.cs:47-50`); MV and counts layout mappings unstated.
- **M5.** Slot eviction policy open (which free slot; more than 8 surfaces). Fix: LRU by decode order; miss → C#.
- **M6.** Option A routing vs corpus: segmentation not excluded from A's eligibility; `error_resilient_mode=1` clears libvpx's segment map and LF deltas, conflicting with C# `_lastSegmentMap`.
- **M7.** Header-writer test cannot call `ReadUncompressedHeader`/`ReadCompressedHeader` (private static, other assembly, `DecodeFrame.cs:1829, 2063`); runners and csproj not listed.
- **M8.** Factory contract test (`android/cs/video/tests/VideoDecoderFactoryContractTests.cs`) not extended; no `AndroidVp9Backend.cs`.
- **M9.** Phase 2 is one undivided change; split into buildable steps (null backend + route tests; shim + backend + replay; registration + flag).
- **M10.** No rule for which decoder wins when C# and libvpx differ.

## Low
- L1 the libvpx build dir is keyed by `H1` (`services/vpx/build.sh:14,19`): the patch hash must go into `H1`; state the patch is inert unless its hook is called (`nvn2_codec.c:633-636` shares libvpx).
- L2 `PictureInfo.BitDepth` comes from `SurfaceParams` ("Not supported on T210"); say what counts as 8-bit.
- L3 specify what a native failure writes to counts/MVs (today a failed C# decode still writes counts, `Vp9Decoder.cs:73`).
- L4 `use_prev_frame_mvs` after key frames: C# also requires `!LastFrameIsKeyFrame` (`PictureInfo.cs:55-61`); explain agreement or force error-resilient.
- L5 speed target 1.0x vs ≥ 0.97x.
- L6 thread default 2 vs "set by measurement".
- L7 Option 0 frame location and superframe handling.

## Verdict
Phase 1 is implementable after small fixes (capture schema M2, vpxbench recipe M3, Android multi-source shim and link/identity lists H5) and should be approved separately. Phase 2 fails the Zero-Decision Rule (C1), its interface cannot compile as described (H3), the call site is missing (H4), and the exact-fallback claim does not hold (stale surface cache H1, segment-map state H2). Phase 3's generated corpus needs an encoder and a guest-driver emulator the document does not provide (H6). Back to the elephant: land Phase 1, then rewrite Phase 2/3 for one chosen option.
