# Native VP9 decoding for Nvdec (libvpx)

* **Author:** Catnip202X
* **Date:** 2026-11-08
* **Issue:** jhaizhou-ops/S2Arm#44 (PR #86 is the partial fix this builds on)

## Overview

Switch games hand video frames to the console's hardware decoder (NVDEC). S2Arm emulates that decoder with
Ryujinx's VP9 decoder, which is written in C#. On Android (ROG Phone 6D, Dimensity 9000+) Persona 4 Golden's
1080p anime cutscenes play at 0.53x real speed before PR #86 and 0.70x after it. The game shows each frame
when it is decoded and does not drop frames to keep up with the audio, so the video runs slow and falls behind
the sound.

This brief asks whether to decode those frames with libvpx, Google's VP9 decoder (C with NEON assembly and
row-level multithreading), and how to feed it, given that NVDEC does not receive a normal VP9 frame.

## Analysis

### What the phone measurements say

- Source: all 38 P4G movies in `data_movie.cpk`, 116,534 frames, 1920x1080, 29.97 fps.
- After PR #86, the decode thread is busy about 69% of wall time and decoding reaches about 21 frames/s.
  The process uses about 2.8 cores on average. Android limits the app to cpus 0-6: the four little cores plus
  three big A710 cores. The X2 prime core is outside the app's cpuset unless the phone's game mode is on.
- The remaining hot code is still the VP9 DSP (prediction, loop filter) plus thread synchronisation inside
  the C# decoder. Hand-written C# vector code is unlikely to close a 1.4x gap on three shared cores. libvpx
  decodes 1080p VP9 several times faster per core than scalar or simple vector C#, but that has not been
  measured on this phone yet. The first step of the plan measures it.

### What NVDEC gives the decoder, and what it does not

For each frame the guest (game plus Nintendo's driver) gives:

- The tile data only. The frame's uncompressed header and compressed header have already been parsed by the
  guest driver and are not in the buffer Ryujinx reads. Ryujinx's `DecodeTiles` starts reading tile sizes at
  byte 0. Whether the full frame is still in guest memory just before that offset is unknown.
- `PictureInfo`: frame type, sizes, quantiser, loop filter, segmentation, tile layout, reference sign bias,
  interpolation filter, tx mode, and flags `IsKeyFrame`, `LastFrameIsKeyFrame`, `FrameSizeChanged`,
  `ErrorResilientMode`, `LastShowFrame`, `IntraOnly`.
- `EntropyProbs`: the full probability tables for this frame. The driver has already applied this frame's
  forward updates (from the compressed header) and, for streams that adapt, its own backward adaptation.
- Guest memory offsets of the Last, Golden and Alt reference surfaces and of the output surface, plus
  colocated motion-vector buffers to read and write.
- It expects back: the decoded picture in the output surface, the motion vectors for the next frame, and the
  symbol counts ("backward updates") that the driver uses to adapt probabilities for the next frame.

It does **not** give: the current frame's `show_frame` (only whether the *previous* frame was shown),
`refresh_frame_flags`, `ref_frame_idx`, `frame_context_idx`, `refresh_frame_context`, or
`frame_parallel_decoding_mode`.

### What P4G's streams use (parsed from the local files, nothing uploaded)

| Field | All 116,534 frames |
|---|---|
| `frame_parallel_decoding_mode` | 1 on every frame (no backward adaptation) |
| `refresh_frame_context` | 1; `frame_context_idx` 0 for shown frames, 1 for hidden frames |
| tile columns | log2 = 2 (4 columns) |
| hidden alt-ref frames | 10,674, each in a superframe with a shown frame |
| key frames | 1,957 |
| segmentation, error-resilient, intra-only, show-existing | none |
| `ref_frame_idx` | (0,1,2) or (0,2,1) |

### Prior art

- **yuzu and its forks** (Eden, Citron, Sudachi; GPL-2.0-or-later, so design only, no code) decode NVDEC VP9
  with FFmpeg. They rebuild an uncompressed header from the NVDEC registers and write a compressed header
  that delta-codes each probability against the previous frame's saved tables. Their limits, from their own
  comments: segmentation unsupported, `show_existing_frame` unsupported, `refresh_frame_context` and
  `frame_parallel_decoding_mode` hard-coded to 1. They find refresh flags and `show_frame` with a one-frame
  lookahead, which delays every output by one frame. That works for them because their VIC reads frames from
  the decoder's own queue. In S2Arm the guest reads the output surface as soon as the decode completes, so a
  lookahead is not usable.
- **Ryujinx upstream** dropped FFmpeg for VP9 and wrote the C# decoder that S2Arm uses now. It kept FFmpeg only
  for H.264 and VP8 (`Ryujinx.Graphics.Nvdec.FFmpeg`).
- **S2Arm's own H.264 path** (`android/cs/video/AvcH264Decoder.cs`, libavc) already rebuilds SPS and PPS from
  NVDEC metadata and fails loudly (`【红】`) on what it cannot represent. That is the house pattern to follow.

### The central problem: probabilities and counts

A VP9 decoder needs the exact probabilities the encoder used. There are two ways to give libvpx the guest's
`EntropyProbs`:

1. **Rebuild headers, stock libvpx.** Write an uncompressed header and a compressed header whose updates turn
   libvpx's starting probabilities into the guest's. If libvpx always starts from default probabilities
   (context 0, never refreshed), the result does not depend on earlier frames. Two gaps remain:
   - The compressed header cannot set some tables: UV mode probabilities and some motion-vector tables
     (motion-vector updates can only carry odd values). Those tables change only through backward
     adaptation, which frame-parallel streams like P4G's never do.
   - Stock libvpx does not hand back symbol counts. For a stream that adapts (`frame_parallel_decoding_mode`
     0), the guest needs those counts to compute the next frame's probabilities. With zero counts the guest
     leaves its probabilities unadapted, the next frame's tables are wrong, and the picture corrupts silently.
     NVDEC does not tell us which kind of stream we have.
2. **Small patch to libvpx (pinned 1.15.0, already built from source).** Add a hook so the decoder takes the
   guest's tables directly, skips its own backward adaptation, takes `use_prev_frame_mvs` from the caller, and
   exports its counts and motion vectors. This behaves like the hardware for every stream, at the cost of
   carrying a patch against libvpx internals.

### Other options weighed

- **Hardware decoders (Android MediaCodec, Apple VideoToolbox).** They need a full VP9 frame, so they have the
  same header and count problems as option 1, plus output latency and no output for hidden frames. VideoToolbox
  VP9 support on iOS is not established. Not proposed as the first step.
- **Keep optimising the C# decoder** (row-level multithreading of tile decode, more vector code). Lower risk,
  but the per-frame work stays several times higher than libvpx's. It does not depend on this design.
- **Turn on the phone's game mode** (opens the X2 core). It is a user setting, not a code change. It may help,
  and it is worth measuring on its own.

## Goals vs. Non-Goals

* **In-Scope Goals:**
  - P4G's cutscenes play at 1.0x on the ROG Phone 6D, measured with the same recording-to-source matching
    used for PR #86 (3 runs, median).
  - Output identical byte for byte to the C# decoder for every frame the native path decodes, checked over all
    116,534 P4G frames offline.
  - Any frame the native path cannot represent exactly goes to the C# decoder or reports `【红】`. Never
    decode a frame wrongly without saying so.
  - One implementation for Android and Apple ARM64. libvpx is built for both, but Android builds it without
    VP9 today (`--disable-vp9`).
* **Out-of-Scope Non-Goals:**
  - 10/12-bit VP9 and profiles 1-3 (NVDEC on Switch is 8-bit 4:2:0).
  - Hardware decoders (MediaCodec, VideoToolbox).
  - Changing what the guest sees (timing, syncpoints, VIC).
  - x86 hosts (the C# decoder already has SSE/AVX there).

## Update after the first Goldfish review (rev 2 of the draft)

- **Brief option 1 (rebuilt headers, stock libvpx) is dropped.** Its workaround for previous-frame motion
  vectors (`error_resilient_mode`) also zeroes the reference sign bias in libvpx. In P4G every one of the
  10,674 frames after a hidden alt-ref has sign bias (0,0,1), so it would corrupt them.
- **New option K:** keep the C# decoder and call libvpx's NEON DSP kernels from it. This avoids the header,
  counts and fallback problems entirely.
- **Brief option 2 is option B in the draft.** Option 0 (original headers still in guest memory) is new and
  depends on a Phase 1 capture.
- **The real budget is about 17 ms per decode, not 33 ms.** The decode thread is already idle about 15 ms per
  shown frame, and there are about 1.10 decodes per shown frame.
- **Mid-stream fallback to the C# decoder is not exact in today's code** (surface cache reuse, segment-map
  state). This is now a stated requirement.

## Key Decisions & Staging for Next Steps

- **Decided:** libvpx is the native decoder. It is already shipped (BSD-3 plus patent grant, version 1.15.0
  pinned) and is the reference implementation, so bit-exactness against the C# port is a fair expectation.
- **Decided:** no lookahead. The output surface must hold the decoded frame when the decode call returns.
- **For the owner to decide (see the draft's options):** stock libvpx with rebuilt headers, limited to streams
  shown to be frame-parallel, versus a pinned libvpx patch that matches hardware behaviour for every stream.
- **First step whichever way:** capture NVDEC inputs on the phone (picture info, probabilities, tile data, and
  whether the full frame sits before the tile data in guest memory). Then measure libvpx's decode time on the
  phone for P4G frames before writing integration code.
