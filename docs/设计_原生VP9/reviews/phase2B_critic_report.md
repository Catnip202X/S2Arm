# Goldfish review: design_phase2_B.md (rev 1)

* **Reviewer:** Design Critic + Structural Goldfish, no prior context
* **Date:** 2026-11-09
* **Target:** `docs/设计_原生VP9/design_phase2_B.md`
* **Checked against:** libvpx 1.15.0 source (scratchpad copy) and `compat/tree/src` (Ryujinx.Graphics.Nvdec, .Nvdec.Vp9, .Video)
* **Standards:** feature-spec-standard, implementation-readiness-standard

Paths below are shortened: `dec/` = `libvpx/vp9/decoder/`, `com/` = `libvpx/vp9/common/`, `Vp9/` =
`compat/tree/src/Ryujinx.Graphics.Nvdec.Vp9/`, `Nvdec/` = `compat/tree/src/Ryujinx.Graphics.Nvdec/`.

## Critical

### C1. The interpolation filter is mapped twice, so non-switchable frames use the wrong filter

- Design line 152 says `PictureInfo.InterpFilter` uses libvpx's enum (0 regular, 1 smooth, ...) and writes the
  inverse of `literal_to_filter` into the header.
- The C# decoder treats the same field as the raw 2-bit bitstream literal. `Vp9/Decoder.cs:23-26` defines
  `LiteralToFilter = { EightTapSmooth, EightTap, EightTapSharp, Bilinear }`, and `Vp9/Decoder.cs:77-80` applies it
  whenever the value is not 4. `Constants.cs:34-44` gives EightTap=0, Smooth=1.
- The result: for guest value 0, C# uses SMOOTH, while libvpx decodes `literal_to_filter[inverse(0)]` = REGULAR.
  Guest values 0 and 1 are swapped. libvpx's encoder often picks a fixed filter per frame, so P4G frames are likely
  hit. Every such frame differs from C#, which breaks Goal 2.
- **Fix:** write the switchable bit as `InterpFilter == 4`. Otherwise write `InterpFilter` itself as the 2-bit
  literal. Put the inverse mapping only in the job generator, which turns libvpx's `cm->interp_filter` back into the
  literal the guest would send. Add a unit test: for each literal 0..3, the libvpx-parsed `cm->interp_filter` equals
  `LiteralToFilter[literal]`.

## High

### H1. The MV export differs from C# for intra blocks inside inter frames

- libvpx `read_intra_block_mode_info` (`dec/vp9_decodemv.c:354-387`) sets `ref_frame = {INTRA_FRAME, NO_REF_FRAME}`
  but never writes `mi->mv`.
- `vp9_read_mode_info` then copies `mi->mv` into `cur_frame->mvs` for every block of an inter frame
  (`dec/vp9_decodemv.c`, the loop after `read_inter_frame_mode_info`).
- `cm->mip` is allocated once (`dec/vp9_decoder.c:147-153`), and `vp9_dec_setup_mi` clears only the grid
  (`dec/vp9_decoder.c:52-57`). So an intra block exports whatever MV an earlier frame left at that position.
- C# gets fresh, zeroed context buffers every job (`Vp9/Common/MemoryAllocator.cs:64`, `allocation.AsSpan().Clear()`),
  so its intra blocks export MV 0.
- The values are never used for prediction: candidates are filtered by `ref_frame > INTRA_FRAME`
  (`dec/vp9_decodemv.c:545-590`). But the MV write buffer bytes differ, which breaks Goal 2, and `shadow` mode will
  report a mismatch on almost every inter frame.
- **Fix:** in the export, write `mv[0] = mv[1] = 0` whenever `ref_frame[0] <= INTRA_FRAME`. The other option is to zero
  `mi->mv` in `read_intra_block_mode_info` when the hook is set. Add a parity case that has intra blocks in inter
  frames.

### H2. The entry point cannot simply call the `decoder_decode` path

Design line 113 says the entry point "calls the normal `decoder_decode` path". Three problems with that:

1. **Null decoder on the first job.** `ctx->pbi` is created lazily on the first `decoder_decode` call
   (`vp9/vp9_dx_iface.c:343-346`). The pre-check and `pbi->s2a_in = in` run before that call, so the first job
   dereferences NULL.
2. **Superframe index parsing.** `decoder_decode` parses a superframe index from the last byte
   (`vp9_dx_iface.c:348`, `dec/vp9_decoder.c:542-561`). A synthesized buffer whose last tile byte matches `110xxxxx`
   returns `VPX_CODEC_CORRUPT_FRAME` before decoding. libvpx's encoder pads only whole chunks, so this case is
   possible for a frame that was split out of a superframe.
3. **Trailing-byte loop.** The serial path keeps calling `decode_one` while non-zero bytes remain after
   `*p_data_end` (`vp9_dx_iface.c:377-390`). `p_data_end` is `vpx_reader_find_end` of the last tile
   (`dec/vp9_decodeframe.c:2430`). Non-zero padding in a guest buffer from another game would trigger a second decode
   of garbage. That second decode fails, sets `need_resync`, and returns an error for a frame that decoded correctly.
- **Fix:** the entry point calls `init_decoder` if `ctx->pbi` is NULL. Then it calls `vp9_receive_compressed_data`
  (or `decode_one`) once, with the exact size, and ignores `p_data_end`. State this in the patch table.

### H3. The job generator's surface-offset model cannot express real refresh patterns

- Design lines 250-251 say "slot i is offset `0x100000 * (i + 1)`, and the output goes to the target slot's offset".
  That breaks in three cases:
  - **Key frame:** it refreshes 8 slots with one picture, so there is no single target slot.
  - **Multi-slot refresh:** with `refresh_frame_flags` like 0x03, two slots must name the same surface. A later
    frame that refreshes only one of them would overwrite a surface the other slot still references.
  - **No refresh:** with `refresh_frame_flags == 0`, there is no target slot. If the output goes to a slot's offset,
    it overwrites a live reference in the simulated guest memory.
- Any of these makes the C# side of the parity harness read wrong references. The test then measures harness
  artifacts, not the decoder.
- Also unspecified:
  - how `show_existing_frame` packets are handled (NVDEC never sees them, but they change the driver's
    `LastShowFrame`);
  - how superframes are split into jobs;
  - how `LastFrameIsKeyFrame`, `LastShowFrame`, `FrameSizeChanged`, `Last/Golden/AltFrameSize` and `CompFixedRef`
    are derived (the latter on single-reference frames too);
  - how `RefFrameSignBias` is reported on error-resilient frames, where libvpx zeroes it
    (`com/vp9_entropymode.c:466`).
- **Fix:**
  - Allocate synthetic offsets per libvpx frame buffer (`RefCntBuffer` index), not per slot.
  - The output offset is the new buffer's offset.
  - A reference offset is the offset of `ref_frame_map[ref_frame_idx[i]]`.
  - Release an offset when its buffer's `ref_count` drops to 0.
  - Write down the flag derivations and the handling of show_existing frames and superframes.
  - Validate the flags too against the 1033 captures, not only the `EntropyProbs`.

## Medium

### M1. The reference-mode and compound checks run on intra frames and can block every key frame

- The hook sets `cm->reference_mode = in->reference_mode` and checks `vp9_compound_reference_allowed` for every frame
  (design line 115).
- On key and intra-only frames, `vp9_setup_past_independence` zeroes `ref_frame_sign_bias`
  (`com/vp9_entropymode.c:466`), so compound is never allowed there. If the guest leaves a non-single
  `ReferenceMode` in `PictureInfo` on a key frame, the key frame returns 1, and native never turns on for that GOP.
- libvpx itself touches the reference mode only for non-intra frames (`dec/vp9_decodeframe.c:2884-2896`).
- **Fix:** do the reference-mode setup and its checks only when `!frame_is_intra_only(cm)`. On intra frames, ignore
  `in->reference_mode` and `in->comp_*`, as C# effectively does.

### M2. The delta-Q fields are unsigned bytes in C#, and the design writes them as "4-bit signed"

- `PictureInfo.YDcDeltaQ/UvDcDeltaQ/UvAcDeltaQ` are `byte` (`Nvdec/Types/Vp9/PictureInfo.cs`). They reach C# as `int`
  0..255 and feed `Math.Clamp(qindex + delta, 0, 255)` (`Vp9/QuantCommon.cs:121,137`).
- A guest value like 0xFE (meaning -2) is +254 to C#. The design does not say how such a value is encoded.
- The bit format is magnitude then sign (`vpx_rb_read_signed_literal`), with range ±15 for Q deltas and ±63 for
  loop-filter deltas. The design's wording "6-bit signed" / "4-bit signed" reads like two's complement.
- **Fix:**
  - If any delta is > 15 as C# reads it, route to C#. That is exact, because C# is the reference.
  - Otherwise write the value as a positive magnitude.
  - The `Lossless` check uses the same reading of the values.
  - Spell out magnitude-then-sign for all signed fields.
  - Map the fields by name: the struct order is YDc, UvAc, UvDc, while the bitstream order is y_dc, uv_dc, uv_ac.

### M3. Argument 4(c) can be made exact instead of falling back to a report

- In an all-C# run, the key frame clears C#'s `_lastSegmentMap`, and non-segmentation frames never write it
  (`Vp9/Decoder.cs:187-231`). `_lastSegmentMapMiRows/Cols` are updated on every frame.
- When native decodes the key frame and the frames after it, C#'s map stays stale from before the key frame. So the
  claim that "the map is not in use" is false at the first switch.
- **Fix:** add a C# method that applies `LoadSegmentMap`'s reset and dimension bookkeeping without decoding. Call it
  for every native job. 4(c) is then exact, and the `【红】` report is not needed.

### M4. "1 = nothing changed" is false on the internal-error path

- When the read_compressed_header check fails, `vpx_internal_error` longjmps to the `setjmp` in
  `vp9_receive_compressed_data` (`dec/vp9_decoder.c` near 455). Then `decode_one` sets `pbi->need_resync = 1` and
  `ctx->need_resync = 1` (`vp9_dx_iface.c:325-330`).
- By that point, `read_uncompressed_header` has already changed decoder state:
  - `last_frame_type` and `last_intra_only`;
  - `width`/`height` and the context buffers;
  - the loop-filter and segmentation state;
  - `cm->counts` (zeroed);
  - on intra frames, `setup_past_independence`, which resets every frame context.
- Every later inter frame then fails with "Keyframe / intra-only frame required" (`dec/vp9_decodeframe.c:2779-2783`).
- The design's routing (native resumes only at a key frame, which clears `need_resync` at
  `dec/vp9_decodeframe.c:2697-2701`) does keep this safe. The API comment and Goal 3 wording are still wrong, and any
  future "stay native after a 1" change would break.
- **Fix:** return 1 only from the pre-decode check, and a separate code (for example 2, meaning "unrepresentable,
  libvpx needs a key frame") from the internal-error path. Document that 2 forces native off.

### M5. The gating synthetic stream cannot exercise tiles or several of the risky features

- At 352x288, `mi_cols` is 44, so there are 6 SB64 columns, so `max_log2_tile_cols` is 0
  (`com/vp9_tile_common.c:45-55`).
- So the gating test never covers:
  - more than one tile column;
  - the per-tile count merge (`dec/vp9_decodeframe.c:2390-2394, 2434-2438`);
  - tile size markers;
  - `Log2TileCols` writing.
- Nothing guarantees that the streams contain:
  - compound frames;
  - fixed filters, in particular literal 0 (smooth, see C1);
  - TX_MODE_SELECT;
  - intra-only frames (libvpx rarely emits them);
  - sizes that are not a multiple of 8;
  - scaled references.
- **Fix:**
  - Add a clip at least 1280 wide with `-tile-columns 2`, a clip with a size that is not a multiple of 8, and a
    resize case.
  - Have `vp9_jobgen_check.sh` print a feature census (counts per feature) and fail if a required feature is absent.

### M6. Native coverage is not measured, and every fallback lasts until the next key frame

- Any reference with no slot, a size mismatch, a result of 1 or an error sends the stream to C# until the next key
  frame (design lines 172-173, 200-202). The C# path is the one that misses Goal 1.
- The P4G key-frame interval is not recorded anywhere.
- **Fix:**
  - The parity harness reports the share of jobs decoded natively and the reason for each fallback.
  - The gate is 100% native on all P4G movies.
  - Record the key-frame interval in phase1_results.
  - Specify the LRU rule: use invalid slots first, and refresh `lastUse` on reference as well as on write.

## Low

- **L1.** The `vp9_mvref_common.c:26` edit is not needed. The decoder uses `dec_find_mv_refs` in
  `dec/vp9_decodemv.c:484` only. `vp9_find_mv_refs` is encoder-only, and the encoder is disabled. Drop it from the
  patch.
- **L2.** Clamping probabilities to 1..255 is harmless for `vpx_read`: prob 0 and prob 1 both give split = 1 for any
  range from 128 to 255. But at prob 0, C# indexes `pareto8_full[-1]`, fails the frame, and writes no picture, while
  native decodes it. Route probability 0 to C# instead of clamping, or document that this one case differs.
- **L3.** Validate `TxMode ≤ 4`, `ReferenceMode ≤ 2`, `InterpFilter ≤ 4`, `Log2TileRows ≤ 2` and
  `RefFrameSignBias ∈ {0,1}` in C# before writing the header. libvpx indexes tables with these values.
- **L4.** `shadow` mode needs scratch MV and counts buffers for the native side. The native side must also not call
  `SurfaceWriter` or `Cache.Invalidate` in that mode, or it corrupts the guest state that C# then reads. The design
  does not say this.
- **L5.** `SurfaceReader` reads only the crop area (`Nvdec/Image/SurfaceReader.cs:29-45`). After a native→C# switch,
  C#'s references hold no decoded pixels outside the crop, while a C# surface found in the cache does. This should
  not change visible pixels, but the forced-fallback case in step 4 should use a size that is not a multiple of 8 to
  prove it.
- **L6.** The comparison against the 1033 captures should compare the converted `Vp9EntropyProbs` fields, not raw
  NVDEC bytes. The NVDEC layout has padding and a 4th coefficient-probability byte that `Convert` ignores.
- **L7.** Document structure:
  - The parent link `./design_draft.md` does not resolve; there is no such file in the directory (lines 6 and 3 of
    phase1_results.md).
  - "see Tests" (line 83) points to a section that does not exist.
  - "Synthetic stream" is an H3 under "Implementation order"; it belongs under a Test plan section.
  - "Open questions: none blocking" is not true while H3's generator semantics are undecided. That also fails the
    zero-decision rule.

## Claims verified as correct

- **Header field order.** The uncompressed-header table matches the order in `read_uncompressed_header`
  (`dec/vp9_decodeframe.c:2640-2863`):
  1. marker, profile, show_existing, type, show_frame, error_resilient;
  2. key frame: sync code, color_space (3 bits), color_range (profile 0 reads no subsampling bits), frame size,
     render size;
  3. non-key frame: intra_only bit (only when show_frame = 0), reset_frame_context;
  4. intra-only: sync code, refresh_frame_flags, frame size, render size;
  5. inter: refresh_frame_flags, 3×(index + sign bias), found_ref bits, frame size, render size, allow_hp, filter;
  6. refresh_frame_context, frame_parallel_decoding_mode, frame_context_idx (2 bits);
  7. loop filter, quantization, segmentation, tiles, 16-bit header size.
- **Read_compressed_header skip.** Skipping it leaves only `tx_mode`, `reference_mode`, `comp_fixed_ref` /
  `comp_var_ref`, the `fc` fields and `new_fb->corrupted` to set. The design covers all of them.
- **Probability and count mapping.** The mapping covers every `FRAME_CONTEXT` field.
- **Counting.** libvpx and C# count the same things:
  - tx counts, partition, skip, coef and eob_branch counts (`dec/vp9_detokenize.c` vs `Vp9/Detokenize.cs`);
  - MV counts with usehp = 1;
  - no uv_mode or y_mode counts on intra frames.
- **Other claims that hold:**
  - key frames force a refresh of 0xFF;
  - error_resilient = 1 would zero the sign bias;
  - skipping adaptation is harmless with refresh_frame_context = 0 (`dec/vp9_decodeframe.c:3049-3065`);
  - intra frames export zero MVs, matching `Vp9Common.GetMvs`;
  - `VP8_SET_REFERENCE` reaches only slots 0-2.

## Verdict

Not ready to implement:
- C1 alone breaks byte parity on fixed-filter frames.
- H1 breaks MV-buffer parity on almost every inter frame.
- H2 crashes on the first job, or rejects valid frames, as written.
- H3 makes the job generator, which feeds Goals 2(a) and 2(c), unable to describe key frames or multi-slot
  refreshes.

The overall approach still holds: skip the compressed header, install the guest's tables, import and export the
MVs, and fall back only at points where the switch is exact. Fix C1, H1-H3 and M1-M4 in the spec. Then re-review
only the changed sections.
