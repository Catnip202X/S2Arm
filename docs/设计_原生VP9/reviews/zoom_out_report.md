# Zoom-out Goldfish: design_draft.md (full file, rev 1)

Reviewer: fresh subagent, persona `agents/zoom_out.md` (elephant-goldfish-model), no conversation context.

No Critical findings.

## High

- **H1. Missing lowest-risk native option: call libvpx's NEON DSP kernels from the existing C# decoder.** The C# decoder is already a stateless, hardware-shaped port (guest `EntropyProbs`, guest references, guest MV buffers, writes counts; `Nvdec.Vp9/Decoder.cs:35-71`). Every hard part of the draft (rebuilt headers, slot table, `show_frame`/`last_show_frame`, counts, libvpx patch, routing) comes from replacing the whole decoder. Replacing only the kernels (prediction/convolve, inverse transforms, loop-filter rows) keeps all guest-visible state in C# and stays bit-exact, because libvpx's NEON kernels must match its C reference. Fix: add it as an option, and have Phase 1 measure it (profile where C# time goes on the phone, time those kernels in libvpx). The likely concern is P/Invoke call granularity.
- **H2. The go/no-go gate is bundled with work that only matters if it passes, and its bar is vague.** The libvpx speed bench needs no capture and no Ryujinx patch. "33 ms with headroom" is not a number and ignores costs that stay on the Nvdec thread after the swap (NV12 block-linear copy, MV/count writes, contention with render threads on the same three A710s; `Vp9Decoder.cs:62-73`). Fix: make the bench step 0 and land it alone, with a numeric bar (p95 ≤ 33.4 ms − measured copy/write-back − margin, 2 threads, under load). Stop if it fails.
- **H3. "Switching from native to C# mid-stream is exact (B)" is false as the code stands.** The C# decoder rents references from `SurfaceCache`; an exact hit (same offsets/owner/size) returns the cached picture without re-reading guest memory (`Image/SurfaceCache.cs:43-58`; reads only on reuse/creation, 78-81, 99-102). After native writes to offsets the C# decoder used earlier, a C# fallback predicts from stale pictures, silently. Fix: every native write to an output offset invalidates the matching cache entry (`SurfaceCache.Invalidate`), and add the case to `tests/vp9_route/`.

## Medium

- **M1. Three integrations are written before the decision that picks one.** Approve Phase 1 plus a shared skeleton (factory slot, per-context backend, output copy, cache invalidation, failure boundary); write Phase 2 as a short follow-up design for the chosen option.
- **M2. Stock libvpx reference controls are not used.** `s2a_vp9_decode` returns only shown frames (`services/vpx/s2a_vp9.c:41-47`), so hidden alt-ref frames have no picture to copy, despite "hidden frames are copied too". libvpx has `VP9_GET_REFERENCE` and `VP8_SET_REFERENCE`/`VP8_COPY_REFERENCE` on slot indices: fetch hidden/refreshed slots, and on a reference miss import the guest surface into a slot instead of refusing (allows C#→native switches at any frame; with truthful `show_frame` 0, intra-only may stay native).
- **M3. Option 0's detection is underspecified; a fixed 4 KiB window is the wrong probe** (a key frame's compressed header size field is 16 bits, up to 64 KiB). Runtime check: scan back for a frame marker, parse the uncompressed header, accept only if header length + `header_size_in_bytes` ends exactly at `SetInBufBaseOffset` and fields agree with `PictureInfo`. Record that distance in the capture.
- **M4. PR #86 is not in this branch.** `SurfaceWriter.cs:106` still has only SSE2; without #86 the native path inherits a scalar NV12 copy on ARM64. State the base.

## Low

- **L1.** Android shim build links only `s2a_vp8.c` (`android/codecs/vpx-build.sh:22`); add `s2a_vp9.c` and hash it into the cache key (Apple already does, `services/vpx/build.sh:14`).
- **L2.** Reuse `VpxSurface` (`Vpx/VpxVp8Decoder.cs:103`) and the Android failure wrapper (`android/cs/video/AndroidVp8Decoder.cs`); keep the new interface minimal until the option is chosen.
- **L3.** `S2A_NVDEC_VP9_THREADS` is speculative; hard-code the measured default.
- **L4.** Add a third reference for P4G: `vpxdec --md5` per frame on the local IVF, to tell which side is wrong when C# and native disagree.

## Verdict

Clear problem and measurement-first, but it solves the hardest version first: replacing the whole decoder creates the header, probability, counts, reference and fallback problems that fill most of the document, and it never weighs moving only the hot kernels into libvpx. Before approval: run the libvpx bench alone as a numeric in-context gate (H2), add and measure the kernel-level alternative (H1), and fix the surface-cache staleness behind B's "exact fallback" (H3). Approve Phase 1 after those edits; defer Phase 2 tables to a follow-up design for the chosen option.
