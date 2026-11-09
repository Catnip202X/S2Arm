# Phase 2 design, option B: NVDEC VP9 jobs decoded by a patched libvpx

* **Author:** Catnip202X
* **Date:** 2026-11-09 (rev 4, after three Goldfish rounds)
* **Status:** approved by the owner on 2026-11-09 (rev 4); implementation in progress. See "Implementation status" below.
* **Parent:** [design_draft.md](./design_draft.md) (rev 3) · **Evidence:** [phase1_results.md](./phase1_results.md) ·
  **Reviews:** [phase2B_critic_report.md](./reviews/phase2B_critic_report.md),
  [phase2B_readiness_report.md](./reviews/phase2B_readiness_report.md)
* **Issue:** jhaizhou-ops/S2Arm#44 · **Base:** PR #86 (`perf/vp9-arm64`) merged into `main` before PR 1 below opens.
  Until then the work branches from `perf/vp9-arm64`.

## Implementation status (2026-11-09)

- **Step 0:** libvpx 1.17.0 fixes VP9 decoder memory-safety bugs (an invalid reference index crash, a heap
  overflow in `vp9_dec_alloc_row_mt_mem`), so the pin moves to 1.17.0 first: #213.
- **PR 1:** #231 (stacked on #213). Phone, 1033 captured P4G jobs, 2 threads: 6.6 ms mean per job while the game
  plays the same movie, 100% native. MVs and counts match the C# decoder on every job, and the pictures match
  stock libvpx decoding the original IVF on all 838 shown frames.
- **The C# decoder had a bug, now fixed in #226.** Its loop filter thresholds (`vp9_loop_filter_init`) were never
  initialised, so deblocking almost never ran. Against libvpx it matched 3 of 838 frames before the fix and 838
  of 838 after. Goal 2 ("byte-identical to the C# decoder") therefore uses the C# decoder **with #226**, which
  must merge before the PR 3 parity gate.
- Phase 1's libvpx numbers below were taken with 1.15.0. The 1.17.0 decoder gives byte-identical output on the
  same streams (#213).

## What changed in rev 4

These resolve [phase2B_rev3_report.md](./reviews/phase2B_rev3_report.md).
- **D1.** The resize clip is now its own stream with lag 0.
- **D2.** `opus-build.sh` is updated to the new `codec_shim` argument order.
- **D3.** The harness buffer is 128 MiB.
- **D4.** vpxbench links the shipped, patched recipe.
- **D5.** `门禁覆盖` globs are separated by spaces.
- **N12.** The remaining wrapper text is removed.

## What changed in rev 3

Each item resolves a finding in phase2B_rev2_report.md.

- **N1.** The probability pre-check covers only the pivot node of reachable coefficient contexts. libvpx's own
  tables keep 144 unreachable band-0 bytes at 0 (§1).
- **N2.** The job generator and the parity harness share a fixed memory map, sized for 1080p, with a
  non-overlap assert (§6).
- **N3.** libvpx's error detail reaches the caller (§1).
- **N4.** PR 1's hooked bench uses a C header writer, which also gives the C# writer a second implementation to
  test against (§7, PR 1).
- **N5.** The resize clip is made with a small encoder driver, so frames reference across the size change.
  Coverage of each fixed filter is conditional (§8).
- **N6, N7.** Build variants `ship`, `nopatch` and `export` come from `services/vpx/build.sh`, and the export
  callback is declared (§1, §7).
- **N8.** The synthetic `.ivf` files are added to the public-repo scan rules.
- **N9, N10.** Test seams, `InternalsVisibleTo`, the manual fuzz test name and the `门禁覆盖` lines are specified.
- **N11.** The device self-test fixture format is defined.
- **N12.** Backend exceptions are caught in routing on every platform, so the Android VP9 wrapper is dropped.
- **N13, N14.** Field names, the recipe key, the `codec_shim` argument order, per-call state, shadow-mode rules
  and where the thread count lives are fixed.

## What changed in rev 2 (since rev 1)

- **Interpolation filter:** `PictureInfo.InterpFilter` is the 2-bit bitstream literal (or 4 for switchable), not
  libvpx's enum, and it is written unchanged. In rev 1 smooth and regular were swapped. In Phase 1's captures 208 of
  1033 jobs use a fixed filter.
- **MV export:** libvpx keeps old `MODE_INFO` contents between frames, while C# starts every frame zeroed. The
  hook now zeroes `cm->mip` before each frame (§1).
- **The entry point** no longer goes through `decoder_decode`. It creates the decoder if needed and calls
  `vp9_receive_compressed_data` once, with the exact size (§1).
- **Result codes:** 1 now means "not representable, nothing changed" and is returned only from checks before
  decoding. A failure after the header has been read returns 2, which means "libvpx needs a key frame" (§1).
- **Reference-mode and compound checks** apply to inter frames only (§1).
- **Routing:** the rev 1 exception is gone. Any job decoded by C# turns native off and clears every slot. Native
  resumes only at a key frame (§4).
- **No probability clamping:** a zero probability goes to C# (§1).
- **Delta-Q and loop-filter deltas:** encoding and ranges are specified (§2).
- **C#'s segment-map bookkeeping** is kept up to date during native runs, so a later switch to C# is exact (§4).
- **Interfaces:** the C# and C interfaces are written out in full (§5). There is a public `IVp9NativeBackend`
  interface, so test spies can implement it. (Rev 3 drops the Android wrapper; see N12.)
- **Job generator:** it is compiled only into the tool's own libvpx. Offsets are assigned per libvpx frame buffer,
  not per slot, and every `PictureInfo` field has a derivation (§6).
- **Build and tests:** the build-list changes move into PR 1, and the patch path is listed everywhere it is an
  input. Tests are named `*_test.sh` and registered in the gate. Pass/fail rules, a census of covered features,
  and a larger synthetic stream with several tile columns are added (§7, §8).
- **Scope:** native becomes the default on Android arm64 first. Apple arm64 registers the backend but stays
  `managed` until a `shadow` run of an adapting title passes. That is an owner decision; see Open questions.
  Intel Macs never register.
- **New items:**
  - step 0, a check of newer libvpx releases;
  - an early speed check of the hooked path;
  - a device self-test in `AndroidCodecs.cs`;
  - an ASan fuzz driver;
  - decision rules for step 7;
  - PR boundaries.

## Summary

Each NVDEC VP9 job becomes one call into libvpx carrying a small patch (`services/vpx/patches/`). C# writes an
uncompressed VP9 header from `PictureInfo`, then a 1-byte stand-in for the compressed header, then the guest's tile
data. The patch adds one entry point. For that call it:
- skips parsing the compressed header and installs the guest's probability tables instead;
- takes the previous frame's MVs from the guest's MV read buffer;
- skips libvpx's backward adaptation;
- returns the picture, the MVs and the symbol counts in the guest's layouts.

The C# decoder remains the fallback.

Phase 1 numbers this rests on:
- libvpx with 2 threads decodes P4G's opening at 5.2 ms mean per packet while the game runs. The budget is
  18.6 ms.
- The guest copies only the tile data, so the original headers are not available.
- The guest feeds each job's MV write buffer back as the next MV read buffer.
- P4G's driver does not read the counts.
- In the 1033 captured jobs:
  - 16 are key frames, about one every 65 jobs;
  - 758 use `ReferenceMode` 2 (select);
  - 208 use a fixed interpolation filter;
  - one is lossless;
  - none uses segmentation.

## Goals

1. P4G's cutscenes play at ≥ 0.97x on the ROG Phone 6D in default mode: the median of 3 runs with PR #86's method.
2. For every job the native path decodes from a conformant stream, the picture, the MV write buffer and the counts
   buffer are byte-identical to what the C# decoder writes. This is checked offline over:
   - (a) all 116,534 P4G frames, turned into jobs by the job generator (§6);
   - (b) the 1033 jobs captured on the phone in Phase 1;
   - (c) the in-repo synthetic streams (§8).
3. A frame the native path cannot represent goes to the C# decoder at a point where the switch is exact. A native
   decode error is reported once per context with `【红】` and the job then goes to C#. Nothing is decoded wrongly
   without a report.
4. On all P4G movies (job generator), ≥ 99% of jobs are decoded natively. Each fallback is listed with its reason.
5. GRID Autosport, the one known title with an adapting stream, keeps decoding correctly. Offline, the
   adapting synthetic stream in the parity gate stands in for it (counts are compared byte for byte). On a device,
   a `shadow` run of GRID's opening is required before native becomes the default on any host where GRID is
   installed. GRID is not on the ROG Phone 6D. Apple is gated on this (see Open questions).

## Non-Goals

- Segmentation. Such frames go to C#.
- 10/12-bit and profiles 1-3.
- Importing guest pictures into libvpx slots. Native resumes only at a key frame.
- x86 hosts, including Intel Macs.
- Hardware decoders.
- A switch to C# in Android release builds (`S2A_*` variables reach only debuggable builds). Rolling back a native
  regression there needs a new build. This is accepted for v1.

## Glossary

- **Job:** one NVDEC VP9 decode request, meaning one VP9 frame, shown or hidden.
- **Slot:** one of libvpx's 8 reference slots (`cm->ref_frame_map`).
- **Guest offset:** a 256-byte-unit offset from the NVDEC registers.
- **Hook:** the per-call inputs and outputs the patch's entry point handles.
- **Native-active:** per decoder context, whether jobs go to libvpx.

## Architecture

```text
NvdecDevice ─ Vp9Decoder.Decode(context, rm, state)
                 │ PictureInfo, EntropyProbs, tile data
                 ├─ route (§4) ──► C#: context.ManagedVp9 (per context)
                 └─► Vp9NativePath (internal, Nvdec)
                        ├─ Vp9HeaderWriter      uncompressed header + 1 byte + tile data
                        ├─ Vp9SlotMap           guest offsets ↔ libvpx slots
                        └─ IVp9NativeBackend.Decode(...)   (public; Vpx.VpxVp9Backend → s2a_vp9_nvdec → patch)
                 ├─ SurfaceWriter.Write(picture) ; rm.Cache.Invalidate(output offsets)
                 ├─ MVs: written by the patch into the guest MV write buffer
                 └─ counts: Vp9BackwardUpdates → BackwardUpdates → guest counts buffer (existing code)
```

## Detailed design

### 1. libvpx patch (`services/vpx/patches/0001-s2a-vp9-nvdec-hook.patch`)

The patch applies to the pinned source with `patch -p1` in both recipes (§7). With no hook set, libvpx behaves
exactly as before, which the identity test in §8 checks. The export mode for the job generator lives inside
`#ifdef S2A_VP9_EXPORT`, which only the `export` build variant defines (§7). Shipped libraries contain the hook
only. The export code is added to the same patch file in PR 2. The patch is an input to both recipe keys, so
changing it there rebuilds correctly.

**Export API** (only under `S2A_VP9_EXPORT`, declared in the same header):

```c
typedef struct {
  /* PictureInfo inputs, read after read_compressed_header, before this frame updates last_* state */
  int32_t width, height, frame_type, intra_only, error_resilient, show_frame;
  int32_t last_show_frame, last_frame_is_key, last_width, last_height;
  int32_t ref_buf[3];            /* cm->frame_refs[0..2].idx (RefCntBuffer index), -1 on intra frames */
  int32_t ref_width[3], ref_height[3];
  int32_t new_buf;               /* cm->new_fb_idx */
  int32_t sign_bias[4], filter_level, sharpness, mode_ref_delta_enabled, ref_deltas[4], mode_deltas[2];
  int32_t base_qindex, y_dc_delta_q, uv_dc_delta_q, uv_ac_delta_q, lossless, tx_mode, allow_hp;
  int32_t interp_literal;        /* bitstream literal, 4 = switchable */
  int32_t reference_mode, comp_fixed_ref, comp_var_ref[2], log2_tile_cols, log2_tile_rows;
  int32_t use_prev_frame_mvs;    /* libvpx's own value, for the census */
  const uint8_t *probs;          /* *cm->fc, converted to the Vp9EntropyProbs layout (S2A_VP9_PROBS_SIZE) */
  const uint8_t *tile_data; size_t tile_size;   /* bytes after the compressed header */
} s2a_vp9_export;
typedef void (*s2a_vp9_export_cb)(const s2a_vp9_export *e, void *user);
void s2a_vp9_set_export_cb(vpx_codec_ctx_t *ctx, s2a_vp9_export_cb cb, void *user);   /* fired once per decoded frame */
```

**Public header `vpx/s2a_vp9_nvdec.h`.** It is copied with the other `vpx/` headers.

```c
#include <stddef.h>
#include <stdint.h>
#include "vpx/vpx_decoder.h"

#define S2A_VP9_PROBS_SIZE  3087   /* Ryujinx Vp9EntropyProbs; both sides assert it */
#define S2A_VP9_COUNTS_SIZE 13208  /* Ryujinx Vp9BackwardUpdates */
#define S2A_VP9_MVREF_SIZE  16     /* Ryujinx Vp9MvRef: int16 row,col ×2, int32 ref ×2 */

typedef struct {
  const uint8_t *probs;           /* S2A_VP9_PROBS_SIZE bytes */
  int32_t intra_frame;            /* 1 for a key or intra-only frame (PictureInfo flags), else 0 */
  int32_t tx_mode;                /* PictureInfo.TxMode, 0..4 */
  int32_t reference_mode;         /* PictureInfo.ReferenceMode, 0..2; ignored on key and intra-only frames */
  int32_t comp_fixed_ref, comp_var_ref0, comp_var_ref1;   /* ignored unless reference_mode != 0 */
  int32_t use_prev_frame_mvs;     /* Vp9PictureInfo.UsePrevInFindMvRefs */
  const uint8_t *prev_mvs;        /* guest MV read buffer, mi_rows*mi_cols entries; NULL when use_prev_frame_mvs 0 */
  uint8_t *mvs;                   /* guest MV write buffer, mi_rows*mi_cols entries, written */
  uint8_t *counts;                /* S2A_VP9_COUNTS_SIZE bytes, written */
} s2a_vp9_nvdec_io;

typedef struct {
  const uint8_t *planes[3]; int32_t strides[3]; int32_t width, height;   /* valid until the next call */
} s2a_vp9_nvdec_picture;

/* S2A_VP9_OK 0          decoded; mvs, counts, picture filled
 * S2A_VP9_UNREPR 1      not representable; libvpx untouched; use C# for this job
 * S2A_VP9_NEED_KEY 2    not representable after the header was read; libvpx needs a key frame
 * S2A_VP9_ERROR -1      decode error; libvpx needs a key frame */
int s2a_vp9_nvdec_decode(vpx_codec_ctx_t *ctx, const uint8_t *frame, size_t size,
                         const s2a_vp9_nvdec_io *io, s2a_vp9_nvdec_picture *pic);
```

The sizes count the C# struct fields: Vp9EntropyProbs is 3087 bytes with no padding bytes (it does hold 144
unreachable band-0 bytes, see the pre-check), and Vp9BackwardUpdates is
3302 uints. Step 1 checks them with `Unsafe.SizeOf` in a C# test and a `_Static_assert` on a C mirror.

**Entry point**, in `vp9/vp9_dx_iface.c`:
1. Pre-checks. Any failure returns 1 before libvpx state changes:
   - `ctx->priv` exists;
   - `frame` is at least 3 bytes;
   - for every tx size, plane and ref, in band 0 contexts 0-2 and bands 1-5 contexts 0-5, the pivot node
     probability `CoefProbs[...][2]` is non-zero. libvpx and C# both index `pareto8_full[p - 1]` with it, so 0
     reads out of bounds in libvpx and fails the frame in C#; such frames go to C#. Other zero probabilities are
     harmless: 0 and 1 give the same split in both bool decoders. Band 0 contexts 3-5 are unreachable and are 0 in
     libvpx's own tables. `SegTreeProb` and `SegPredProb` are unused. Neither is checked;
   - when `use_prev_frame_mvs` is set, every guest MV ref is in -1..3;
   - for a key or intra-only frame (`io->intra_frame`; the header written from the same flags carries the same
     frame type), the guest's `KfYModeProb`, `KfUvModeProb` and `KfPartitionProb` equal
     libvpx's `vp9_kf_y_mode_prob`, `vp9_kf_uv_mode_prob` and `vp9_kf_partition_probs`.
2. If `ctx->pbi` is NULL, call `init_decoder(ctx)` (static in the same file; it applies the row-MT and thread
   settings stored by `s2a_vp9_open`). If that fails, return -1 without touching `pbi`.
3. Reset the per-call state `pbi->s2a_unrepr = 0` and `cm->s2a_prev_mvs = NULL`. Set `pbi->s2a_io = io`. Call `vp9_receive_compressed_data(pbi, size, &data)` once. Do not parse a superframe
   index. Ignore the returned end pointer: the frame is exactly the buffer.
4. Clear `pbi->s2a_io`. If the call failed, call `update_error_state(priv, &cm->error)`, set `ctx->err` to its
   result (so `vpx_codec_error_detail` returns libvpx's message), set `pbi->need_resync = 1`, and return 2 when
   `pbi->s2a_unrepr` (set by the checks below) is set, else -1.
5. On success, clear `ctx->err` and fill `pic` from `cm->frame_to_show`.

The decoder-owned `MV_REF` import buffer is freed in `vp9_decoder_remove`.

**Inside decoding** (only when `pbi->s2a_io` is set):

| Where (libvpx 1.15.0) | Change |
|---|---|
| `vp9_decode_frame` (`vp9_decodeframe.c`), right after `read_uncompressed_header` | `memset(cm->mip, 0, cm->mi_alloc_size * sizeof(*cm->mip))`. This mirrors C#'s cleared allocation, so intra blocks and the unused `mv[1]` export zeros. 1080p is about 32k entries. |
| `vp9_decode_frame` `:2986` | `cm->use_prev_frame_mvs = io->use_prev_frame_mvs`. When it is set, convert `io->prev_mvs` (Vp9MvRef) into a decoder-owned `MV_REF` buffer, grown as needed, and set `cm->s2a_prev_mvs` to it. |
| `read_compressed_header` `:2866` | Read nothing. `cm->tx_mode` is `ONLY_4X4` if `xd->lossless`, else `io->tx_mode`; if lossless and `io->tx_mode != ONLY_4X4`, mark the frame not representable. Inter frames only: `cm->reference_mode = io->reference_mode`; if not single, it requires `vp9_compound_reference_allowed(cm)` and then `vp9_setup_compound_reference_mode(cm)`, whose `comp_fixed_ref` / `comp_var_ref` must equal the guest's, otherwise mark it not representable. Copy the probabilities into `*cm->fc` (mapping below). "Not representable" sets `pbi->s2a_unrepr = 1` and calls `vpx_internal_error(…, VPX_CODEC_UNSUP_FEATURE, …)`. |
| `vp9_decodemv.c:493` | `prev_frame_mvs` uses `cm->s2a_prev_mvs` when set. (`vp9_mvref_common.c` is encoder-only and is not touched.) |
| `vp9_decode_frame` `:3050` | Skip `vp9_adapt_*`. `refresh_frame_context` is 0, so the result would be discarded anyway. |
| end of `vp9_decode_frame` | Counts: `cm->counts` → `io->counts`. MVs: for a key or intra-only frame, `io->mvs` is zeroed (as `Vp9Common.GetMvs`); otherwise `cm->cur_frame->mvs` converted to Vp9MvRef (int8 ref → int32, MVs copied). |

The header always carries `frame_parallel_decoding_mode` 0, so libvpx counts, including the per-tile merge in row-MT
(`:2391`, `:2435`). Estimated size: about 300 lines of C.

**Probability mapping** (Vp9EntropyProbs → `FRAME_CONTEXT`):
- One to one: `YModeProb`, `UvModeProb`, `PartitionProb`, `CoefProbs`, `SwitchableInterpProb`, `InterModeProb`,
  `IntraInterProb`, `CompInterProb`, `SingleRefProb`, `CompRefProb`, `Tx8x8/16x16/32x32Prob` (into `tx_probs`) and
  `SkipProb`, each to the matching libvpx field.
- MV tables are transposed: `Joints` → `nmvc.joints`, and `X[c]` → `nmvc.comps[c].x` for `Sign`, `Classes`, `Class0`,
  `Bits`, `Class0Fp`, `Fp`, `Class0Hp` and `Hp`.
- `KfYModeProb`, `KfUvModeProb` and `KfPartitionProb` are checked, not copied. `SegTreeProb` and `SegPredProb` are
  unused, because segmentation is excluded.
- The counts mapping is the reverse: `FRAME_COUNTS` → Vp9BackwardUpdates, with `mv.comps[c].x` → `X[c]`,
  `tx.p8x8` → `Tx8x8` and so on. `tx.tx_totals` has no C# field and is dropped.

### 2. Uncompressed header (`Vp9HeaderWriter`, C#, internal)

Fields in libvpx's read order. Signed fields use VP9's form: magnitude bits, then a sign bit.

| Field | Value | Source / rule |
|---|---|---|
| frame_marker, profile, show_existing_frame | 2, 0 (2 bits), 0 | 8-bit 4:2:0 only |
| frame_type | 0 key / 1 inter | `Flags.IsKeyFrame` |
| show_frame | 0 if intra-only, else 1 | the picture always comes from `frame_to_show` |
| error_resilient_mode | 0 | 1 would zero the sign bias |
| key: sync code, color_space 0 (3 bits), color_range 0, width-1, height-1 (16 bits each), render_and_frame_size_different 0 | | `CurrentFrameSize` |
| non-key: intra_only (only when show_frame 0) | | `Flags.IntraOnly` |
| non-key: reset_frame_context (2 bits) | 0 | contexts are never read |
| intra-only: sync code, refresh_frame_flags (8), size, render 0 | | §3 |
| inter: refresh_frame_flags (8); then for Last, Golden, Alt: ref_frame_idx (3) and sign_bias (1) | | §3; `RefFrameSignBias[1..3]`, which must be 0 or 1, else C# |
| inter: found_ref ×3 = 0, width-1, height-1, render 0 | | `CurrentFrameSize` |
| inter: allow_high_precision_mv | | `AllowHighPrecisionMv` |
| inter: is_filter_switchable; if 0, the 2-bit literal | `InterpFilter == 4` ? 1 : 0, then `InterpFilter` unchanged | the value is the bitstream literal; C# applies `LiteralToFilter` to it (`Nvdec.Vp9/Decoder.cs:23-26,77-80`). Values > 4 go to C# |
| refresh_frame_context, frame_parallel_decoding_mode | 0, 0 | |
| frame_context_idx (2) | 0 | |
| filter_level (6), sharpness (3) | | `FirstLevel` ≤ 63, `SharpnessLevel` ≤ 7, else C# |
| mode_ref_delta_enabled; if 1: mode_ref_delta_update 1, then 4 × (1, 6-bit magnitude, sign) and 2 × (1, 6-bit magnitude, sign) | | `Lf.ModeRefDeltaEnabled`, `Lf.RefDeltas`, `Lf.ModeDeltas` (sbyte); \|value\| ≤ 63, else C#. All deltas are sent every frame, because libvpx keeps deltas between frames |
| base_q_idx (8) | | `BaseQIndex` |
| delta_q y_dc, uv_dc, uv_ac: coded bit, then 4-bit magnitude and sign | in that order, by name: `YDcDeltaQ`, `UvDcDeltaQ`, `UvAcDeltaQ` | the fields are `byte` and C# reads them as 0..255; a value > 15 goes to C# (C# is the reference); 1..15 is written as a positive magnitude |
| segmentation_enabled | 0 | `Seg.Enabled` must be 0 (§4) |
| tile_cols: increment bits from libvpx's `min_log2` to `max_log2` for the width | | `Log2TileCols` within [min, max], else C# |
| tile_rows: 1 bit, plus a second bit if the first is 1 | | `Log2TileRows` ≤ 2, else C# |
| header_size_in_bytes (16) | 1 | then one zero byte, which is never parsed |

C# checks before the call; a failure goes to C#:
- `TxMode` ≤ 4;
- `ReferenceMode` ≤ 2;
- `Lossless` equals (`BaseQIndex == 0` and all three delta-Q values are 0);
- `PictureInfo.BitDepth` is 8 or 0 (T210 does not set it).

### 3. Slot map (`Vp9SlotMap`, C#, internal, one per native decoder)

`Slot[8] = { lumaOffset, chromaOffset, width, height, valid, lastUse }`. `lastUse` is a per-context job counter.

- **Key frame:** libvpx refreshes all 8 slots. All 8 become the output offsets, valid, with `lastUse = now`.
- **Inter frame:**
  - For each of the guest's Last, Golden and Alt surfaces, `ref_frame_idx[i]` is the lowest valid slot whose
    luma offset, chroma offset, width and height match. If no slot matches, the frame is not representable: go to
    C# (§4). Each referenced slot gets `lastUse = now`.
  - **Target slot:** the first rule that applies wins:
    1. a valid slot holding the output luma offset that this frame does not reference;
    2. an invalid slot;
    3. the valid slot not referenced by this frame with the smallest `lastUse`, lowest index on ties.

    If the output luma offset equals a referenced surface (decoding in place), go to C#.
  - `refresh_frame_flags = 1 << target`.
- **Intra-only frame:** no references. The target is chosen as for inter frames, and `refresh_frame_flags =
  1 << target`.
- **After a native decode to output luma offset X:** the target slot holds X (valid, `lastUse = now`). Every other
  slot with luma offset X becomes invalid, because guest memory at X now holds the new picture.
- **Reset:** every slot becomes invalid whenever native-active turns off.

### 4. Routing (`Vp9Decoder.Decode`, per `NvdecDecoderContext`)

Context state:
- `ManagedVp9`: the C# `Decoder`, moved from today's static field into the context;
- `Native`: a `Vp9NativePath`, or null when no backend is registered or the backend has failed;
- `NativeActive`, false at start.

1. **Native eligibility for a job:** `Native` exists, the mode is not `managed`, and either `NativeActive` is true
   or the job is a key frame. On a key frame with `NativeActive` false, set `NativeActive = true` and reset the
   slots. Then:
   - `Seg.Enabled` must be 0;
   - the §2 checks must pass;
   - the slot map must resolve every reference.

   If any of these fails: `NativeActive = false`, slots reset, and this job goes to C#.
2. **Backend results:**
   - 1: this job goes to C#, `NativeActive = false`, slots reset.
   - 2 or -1: the same. In addition, -1 logs one `【红】` per context with the job index and libvpx's error
     string; after 3 errors in one context, `Native` is disabled for that context.
   - A backend exception (`DllNotFoundException`, `EntryPointNotFoundException`, `BadImageFormatException`) is
     caught in `Vp9Decoder` routing on every platform: one `【红】` per process, a static `_nativeDisabled = true`,
     and this job goes to C#. No platform wrapper is needed for VP9.
3. **Any job decoded by C# leaves `NativeActive` false.** Native resumes only at a key frame.
4. **Why native → C# at a non-key frame is exact:**
   - (a) every native write calls `rm.Cache.Invalidate(lumaOffset, chromaOffset)`, so C# rents those surfaces fresh
     and reads guest memory, which holds the native output;
   - (b) C# reads MVs from the guest MV write buffer the native path filled in the same layout, and that buffer is
     byte-identical to C#'s own (Goal 2);
   - (c) for every native job, `ManagedVp9.TrackSegmentMap(ref pictureInfo)` runs the reset and dimension
     bookkeeping of `LoadSegmentMap` without decoding (new method), so C#'s segment state equals an all-C# run.
     Segmentation is off on every native frame, so nothing else changes;
   - (d) probabilities come from the guest on every job.
5. **Mode switch** `S2A_NVDEC_VP9=managed|native|shadow`, read once per process. The platform default is used when
   it is unset (§5). Test seam: `internal static string ModeOverride` and `internal static Func<int, bool>
   ForceManaged` (job index → force C#) on `Vp9Decoder`. They are set only by tests through `InternalsVisibleTo`,
   and they take precedence over the environment.
6. **The native path rents no surfaces from `rm.Cache`.** Today's code rents all four before decoding
   (`Nvdec/Vp9Decoder.cs:29-32`). That stays on the C# branch only.
   - `shadow` (debug builds only):
     - C# decodes the job and its output is what the guest gets.
     - The native path decodes the same job into scratch buffers: a private surface, scratch MV and counts
       arrays. It does not call `SurfaceWriter` or `Cache.Invalidate`.
     - Hashes of picture, MVs and counts are compared, and the first mismatch per context is reported with
       `【红】`.
     - The native slot map tracks C#'s output offsets as if native had written them. That holds because the
       outputs are equal while there is no mismatch.
     - Rule 3 does not apply in `shadow`: native stays active alongside C#. After the first mismatch, native stops
       for that context until the next key frame.
     - The picture hash covers visible rows only.

### 5. Interfaces and platform wiring

**Public interface** (`compat/tree/src/Ryujinx.Graphics.Video/IVp9NativeBackend.cs`):

```csharp
public enum Vp9NativeResult { Decoded = 0, NotRepresentable = 1, NeedsKeyFrame = 2, Error = -1 }

[StructLayout(LayoutKind.Sequential)]
public struct Vp9NativeParams
{
    public int IntraFrame, TxMode, ReferenceMode, CompFixedRef, CompVarRef0, CompVarRef1, UsePrevFrameMvs;
}

public readonly struct Vp9NativePicture
{
    public readonly nint Y, U, V;
    public readonly int YStride, UvStride, Width, Height;
    public Vp9NativePicture(nint y, nint u, nint v, int yStride, int uvStride, int width, int height) { … }
}

public interface IVp9NativeBackend : IDisposable
{
    // frame: header + 1 byte + tile data. probs: Vp9EntropyProbs bytes (3087). prevMvs: guest MV read buffer
    // (empty when UsePrevFrameMvs is 0). mvsOut / countsOut: written on Decoded only.
    Vp9NativeResult Decode(ReadOnlySpan<byte> frame, ReadOnlySpan<byte> probs, in Vp9NativeParams p,
                           ReadOnlySpan<byte> prevMvs, Span<byte> mvsOut, Span<byte> countsOut,
                           out Vp9NativePicture picture, out string error);
}
```

**C shim** (`services/vpx/s2a_vp9.c`), next to the existing open/decode/close:

```c
int s2a_vp9_nvdec(void *h, const uint8_t *frame, uint32_t size, const s2a_vp9_nvdec_io *io,
                  s2a_vp9_nvdec_picture *pic, const char **error);   /* error: vpx_codec_error_detail or NULL */
```

C# `[LibraryImport("s2a_vpx")]` mirrors `s2a_vp9_nvdec_io` and `s2a_vp9_nvdec_picture` as sequential structs of
pointers and ints.

| File | Change |
|---|---|
| `Ryujinx.Graphics.Video/IVp9NativeBackend.cs` (new) | interface and types above |
| `Ryujinx.Graphics.Nvdec/Vpx/VpxVp9Backend.cs` (new, public sealed) | implements the interface. The libvpx handle is opened lazily on the first `Decode` with `s2a_vp9_open(Threads)`, and closed in `Dispose`. `public static int Threads = 2` is the one place the thread count lives; PR 5 may change the constant |
| `Ryujinx.Graphics.Nvdec/Vp9NativePath.cs` (new, internal) | header writer, slot map, the checks, the call, `VpxSurface` picture → `SurfaceWriter` |
| `Ryujinx.Graphics.Nvdec/S2AVideoDecoders.cs` | `public static Func<IVp9NativeBackend> Vp9`, `CreateVp9()` through the existing guarded `Create`; `public static string Vp9DefaultMode` (`"native"` or `"managed"`) |
| `Ryujinx.Graphics.Nvdec/NvdecDecoderContext.cs` | `ManagedVp9`, `Native`, `NativeActive`; `Dispose` releases both |
| `Ryujinx.Graphics.Nvdec/NvdecDevice.cs:77` | `Vp9Decoder.Decode(_currentContext, _rm, ref _state.State)` |
| `Ryujinx.Graphics.Nvdec/Vp9Decoder.cs` | routing (§4); counts and MV write-back for both paths |
| `Ryujinx.Graphics.Nvdec.Vp9/Decoder.cs` | `TrackSegmentMap` (§4, 4c) |
| `Ryujinx.Graphics.Nvdec/Image/SurfaceCache.cs` | `Invalidate(uint luma, uint chroma)`: entries whose offsets match get their owner, offsets and size cleared, so `Get` re-creates and re-reads them (not offset 0, which `Get` treats as "do not read") |
| `android/cs/video/AndroidVideo.cs` | `Vp9 = static () => new VpxVp9Backend()`, `Vp9DefaultMode = "native"` (exceptions are handled in routing, §4) |
| `platform/apple/cs/video/AppleVideo.cs` | if `RuntimeInformation.ProcessArchitecture == Architecture.Arm64`: `Vp9 = () => new VpxVp9Backend()`, `Vp9DefaultMode = "managed"` |
| `android/cs/aot/AndroidCodecs.cs` | device self-test with 3 ready-to-call units: a key frame and 2 inter frames of `fp_ragged.ivf`. Each unit holds the frame bytes (header + 1 byte + tile data), the 3087 probability bytes and the `Vp9NativeParams` ints, embedded as base64 like `Vp8Frames`. The units are generated on the Mac by the PR 3 header writer and a small dump mode of the parity harness. The test calls `s2a_vp9_nvdec` directly and chains MVs (unit k's `prev_mvs` is unit k-1's output). FNV-1a-64 hashes of the planes, MVs and counts must equal the values recorded on the Mac |

The Phase 1 capture/perturb hook (`S2AVp9Capture.cs`) stays. It is debug-only and feeds §6's check.

### 6. Job generator (`tools/vp9_jobgen/`, C, own libvpx build with `-DS2A_VP9_EXPORT`)

`vp9_jobgen <in.ivf> <out_dir>` writes one S2V9 record (Phase 1 layout, version 1) per decoded frame. Each
superframe is split into its frames, hidden ones first, as the guest does. `show_existing_frame` packets produce no
job.

**Driver model.** Each libvpx frame buffer (`RefCntBuffer` index `b`, at most 12) has a fixed synthetic surface.
libvpx reuses a buffer only when no slot references it, so writing a new picture to its offset never overwrites a
live reference, just as with a real driver.

**Memory map** (offsets in 256-byte units; the job generator and the parity harness use the same map in a flat
128 MiB buffer):

| Item | Offset | Room |
|---|---|---|
| `PictureInfo` | `0x0100` | 256 B |
| `EntropyProbs` | `0x0200` | 4 KiB |
| counts buffer | `0x0300` | 64 KiB |
| MV buffer A / B (alternating; read = previous write) | `0x1000` / `0x2000` | 1 MiB each |
| tile data (`SetInBufBaseOffset`) | `0x4000` | 12 MiB |
| surface `b` luma | `0x40000 + b * 0x4000` | 4 MiB per surface |
| surface `b` chroma | luma + `0x2400` | |

The harness asserts that no live surface overlaps: the block-linear size `SurfaceWriter` computes for luma must be
≤ `0x2400` units, and luma plus chroma ≤ `0x4000` units. Both hold up to 1920x1088.

| Record field | Derivation (libvpx state while decoding the frame, read through the export hook) |
|---|---|
| output offsets | buffer `cm->new_fb_idx` |
| Last / Golden / Alt offsets | buffers `cm->frame_refs[0..2].idx` (the export field `ref_buf`). For key and intra-only frames, the previous frame's values are kept, as a driver would leave them |
| `CurrentFrameSize`, `Last/Golden/AltFrameSize` | `cm->width/height`; the referenced buffers' `y_crop_width/height` |
| `Flags.IsKeyFrame`, `IntraOnly`, `ErrorResilientMode` | `cm->frame_type`, `cm->intra_only`, `cm->error_resilient_mode` |
| `Flags.LastShowFrame`, `LastFrameIsKeyFrame`, `FrameSizeChanged` | `cm->last_show_frame`, `cm->last_frame_type == KEY_FRAME`, `width/height != last_width/last_height`, read before this frame updates them |
| `RefFrameSignBias` | `cm->ref_frame_sign_bias` after the header (zeroed by libvpx on error-resilient frames; such frames are counted in the census) |
| loop filter, quantizer, `Lossless`, `TxMode`, `AllowHighPrecisionMv`, `ReferenceMode`, `CompFixedRef`, `CompVarRef`, tiles | the matching `cm` / `xd` fields after `read_compressed_header`. `InterpFilter` is written as the bitstream literal (the inverse of `literal_to_filter`), 4 for switchable |
| `EntropyProbs` | `*cm->fc` right after `read_compressed_header`, in the NVDEC layout (inverse of `EntropyProbs.Convert`), with the key-frame tables set to libvpx's constants |
| tile data, `BitstreamSize` | bytes after the compressed header to the end of the frame |

The generator also checks every frame and counts the frames where these differ:
- C#'s `UsePrevInFindMvRefs`, computed from the record flags, against libvpx's `use_prev_frame_mvs`;
- the generator's `EntropyProbs` (converted to Vp9EntropyProbs) against `*cm->fc`, as a round-trip check.

A difference in the first is a C# vs libvpx disagreement (C# uses the current frame's `IntraOnly` where libvpx uses
`last_intra_only`). Those frames are listed in the census. They stay in Goal 2(a), because the native path takes
C#'s value through the hook, so native and C# still agree.

**Validation against the device.** For movie 37's first 1033 frames, the generated records must equal the Phase 1
captures field by field:
- the `PictureInfo` fields above, excluding offsets and `Unknown*`;
- `EntropyProbs` compared after `Convert`, so NVDEC padding and the 4th coefficient byte are ignored;
- the tile data.

## 7. Build changes (all in PR 1)

| File | Change |
|---|---|
| `services/vpx/patches/0001-s2a-vp9-nvdec-hook.patch` (new) | the patch (§1) |
| `services/vpx/build.sh` | new `S2A_VPX_VARIANT=ship\|nopatch\|export` (default `ship`). `ship` applies the patch with `patch -p1`; `nopatch` skips it; `export` applies it and adds `-DS2A_VP9_EXPORT` to `--extra-cflags`, and also links a Mac dylib `libs2a_vpx_export.dylib` (libvpx + shims) for the C# tests. The variant name and the patch bytes go into `H1` (and so into `KEY`), so the three never share a cache entry and use the same configure flags otherwise |
| `android/codecs/vpx-build.sh` | `--disable-vp9-encoder --enable-vp9-decoder` in place of `--disable-vp9`; apply the patch; line-1 comment updated; recipe key `codec_recipe_key vpx "$vpx_sha" <(cat "$repo_dir/android/codecs/vpx-build.sh" "$repo_dir/services/vpx/patches/0001-s2a-vp9-nvdec-hook.patch")`; shim call `codec_shim s2a_vpx "$target/libvpx.a" "$target/include" "$target" "$repo_dir/services/vpx/s2a_vp8.c" "$repo_dir/services/vpx/s2a_vp9.c"` |
| `android/codecs/cache.sh` | `codec_shim name archive include parent source...`: the sources move to the end as a list; the key hashes every source in order; each is compiled to its own object |
| `android/codecs/opus-build.sh` | its `codec_shim` call is updated to the new argument order (behavior unchanged) |
| `services/vpx/s2a_vp9.c` | add `s2a_vp9_nvdec` |
| `android/core-link/link_symbols.py` | add `services/vpx/s2a_vp9.c` to the shim tuple (`:845-850`); definitions are collected from the shims, so there is no separate export list |
| `android/docker/cache-identity.py`, `android/docker/test_gate_apk.py`, `android/doctor/test_doctor.py` | add `services/vpx/s2a_vp9.c` and `services/vpx/patches/0001-s2a-vp9-nvdec-hook.patch` wherever `s2a_vp8.c` is listed |
| `tools/public_repo_scan_rules.json` | add `tests/data/vp9_synth/*.ivf` to `binary_ascii_globs` |
| `android/codecs/vpxbench/` | `build.sh` stops configuring its own libvpx and links the shipped Android recipe's output (`build_vpx` from `vpx-build.sh`, which now has VP9 and the patch) plus `s2a_vp9.c`, so the bench measures the shipped library; `hdr.c` (new) is a C header writer mirroring §2, about 150 lines; `vpxbench.c` gains `--jobs <capture_dir>` (hooked mode, see PR 1) |

## 8. Tests

| Test | Kind | Pass rule |
|---|---|---|
| `tests/vpx_patch_identity_test.sh` | gate; needs Xcode (`contrib_tools.tsv`); `# 门禁覆盖：services/vpx/**` | `tests/vpx_patch_identity/md5dec.c` (a 40-line `vpx_codec_decode` + MD5 of every shown frame) gives the same per-frame MD5 list when linked against the `ship` and the `nopatch` variants of `services/vpx/build.sh`, on both synthetic streams. Add `# 门禁预算：` if two cold builds exceed 600 s on the gate host |
| `tests/vp9_jobgen_test.sh` | gate; Xcode; `# 门禁覆盖：services/vpx/** tools/vp9_jobgen/**` | on both synthetic streams: the round-trip checks report 0 differences; the census shows every required feature (below); "jobs rejected by the probability pre-check" is 0. When `S2A_P4G_IVF_DIR` and `S2A_P4G_CAPTURE_DIR` are set (local only), the test also runs the 1033-capture comparison. That is an environment-gated branch inside the gate test, not a separate whitelisted file |
| `tests/vp9_native_parity_test.sh` + `tests/vp9_native_parity/` (csproj) | gate; .NET 10 SDK, Xcode | replays S2V9 records through the C# path and the native path, using `Vp9Decoder.Decode` with a test `IVirtualMemoryManager` backed by one flat 128 MiB buffer (memory map in §6), reached through `InternalsVisibleTo` (below). Each path gets its own `NvdecDecoderContext`, `SurfaceCache` and flat memory; the harness selects paths with `Vp9Decoder.ModeOverride` and `ForceManaged`. Picture, MV and counts bytes must be equal for every native job. Prints the share decoded natively and every fallback with its reason. The forced-fallback case makes job N of the 1280x720 stream go to C# and checks that every later job equals an all-C# run. Gate input: synthetic streams. Local input: P4G job-generator output and the 1033 captures (Goal 4 checked here) |
| `tests/vp9_header_writer_test.sh` (csproj) | gate; .NET, Xcode | for every frame of both synthetic streams, the C# writer's output is byte-equal to `vpxbench/hdr.c`'s for the same record, and parsing it with `libs2a_vpx_export.dylib` gives the record's fields back; a case per interpolation literal 0..4 |
| `tests/vp9_slotmap_test.sh` (csproj) | gate; .NET | cases: key frame, LRU with ties, invalid-first, in place → C#, missing reference → C#, duplicate offsets invalidated |
| `tools/vp9_fuzz/` + `tests/t_vp9_fuzz.sh` | manual (`# 门禁手动：分钟级 ASan 模糊测试`); not matched by the gate's `*_test.sh` | C driver linked with an ASan build (`export` variant plus `-fsanitize=address`) of the patched libvpx and `hdr.c`. 10^4 mutations of the 1033 captures and the synthetic records (bit flips in tile data, probabilities, sizes, MV refs), fixed seed. Pass: no ASan report, no crash |

C# test access: one compat patch adds `InternalsVisibleTo` for `Vp9NativeParity`, `Vp9HeaderWriterTest` and
`Vp9SlotMapTest` to `Ryujinx.Graphics.Nvdec` (precedent: `compat/patches/0002-hle-internals-visible.patch`). The C#
tests carry `# 门禁覆盖：compat/tree/src/Ryujinx.Graphics.Nvdec/** compat/tree/src/Ryujinx.Graphics.Nvdec.Vp9/**
compat/tree/src/Ryujinx.Graphics.Video/*Vp9* services/vpx/**`. Globs are separated by spaces, which is how
`tools/gate_route.py` splits them.
| `AndroidCodecs.cs` VP9 case | device self-test | hashes equal the Mac values |
| `VideoDecoderFactoryContractTests` | existing gate | Vp9 cases: null factory, throwing factory, one instance per context |

**Synthetic streams** (`tests/data/vp9_synth/`, with `make.sh` and a README on provenance; three files, under 1.5 MB together; the census below is taken over all three). Made once
with ffmpeg + libvpx-vp9 from `testsrc2` and `mandelbrot` patterns. No game data.
- `adapt_720.ivf`: 1280x720, 48 frames, ffmpeg + libvpx-vp9 with `-tile-columns 2 -frame-parallel 0
  -auto-alt-ref 1 -lag-in-frames 16 -g 16`: adaptation, hidden frames, compound, 4 tile columns.
- `resize.ivf`: 640x360 → 480x272, 32 frames, encoded by `tests/data/vp9_synth/resize_enc.c`. That is about 100
  lines, built by `make.sh` against a local encoder-enabled libvpx 1.15.0 that is not part of any recipe. Settings:
  frame-parallel off, `g_lag_in_frames` 0 (libvpx refuses a size change when lag > 1, `vp9_cx_iface.c:809`). At
  frame 16 it calls `vpx_codec_enc_config_set` with the new size and no key frame, the pattern of libvpx's
  `test/resize_test.cc`, so inter frames reference across the size change (scaled references).
- `fp_ragged.ivf`: 350x198 (not a multiple of 8), `-frame-parallel 1 -g 16`, 48 frames, using P4G's setting.

Required census, failing if any count is 0:
- key frames, at least 2 of them;
- hidden frames;
- compound frames;
- at least one fixed-filter frame, and switchable;
- `TX_MODE_SELECT`;
- more than one tile column;
- a resize with scaled references;
- intra blocks in inter frames.

libvpx's encoder picks the frame filter from statistics, so each literal is not guaranteed. Every literal 0..4 is
covered by the header-writer test instead. The README records the actual census of the committed files.

Intra-only frames are not produced by libvpx's encoder. They are covered only by the header-writer test.

## Implementation order and PR boundaries

0. **libvpx version check (no PR unless needed).** Read the CHANGELOG of 1.15.1, 1.15.2 and 1.16.0 for decoder
   fixes. If any fixes a VP9 decoder memory-safety bug, bump the pin (`services/vpx/build.sh` `VER/SHA`) in its own
   PR first, and write the patch against the new pin.
1. **PR 1: patch + recipes + lists + identity test + synthetic streams.** Nothing calls the hook yet. Record the
   `libs2arm_android.so` size delta in the PR. Hooked bench: `vpxbench --jobs <capture_dir> 2` reads the S2V9
   captures in order, starting at the first key frame. For each job it writes the header with `hdr.c` and the slot
   policy of §3 (C mirror, about 60 lines), calls `s2a_vp9_nvdec`, and passes job k's output MVs as job k+1's
   `prev_mvs`. It reports per-job mean and p95, and the share of jobs returning 0. Pass: mean ≤ 7 ms per job with
   2 threads on the phone (Phase 1: 5.2 ms per packet at about 1.10 jobs per packet ≈ 4.7 ms per job, plus counting
   and MV conversion), and ≥ 99% of jobs return 0. Otherwise stop and report before PR 3.
2. **PR 2: job generator + `vp9_jobgen_test.sh`** (its own libvpx build).
3. **PR 3: `IVp9NativeBackend`, `VpxVp9Backend`, `Vp9NativePath` (header writer, slot map), `SurfaceCache.Invalidate`,
   `TrackSegmentMap`, routing, with the factory unregistered.** Tests: header writer, slot map, parity (gate and
   local), fuzz (manual, reported in the PR).
4. **PR 4: registration** (Android default native; Apple arm64 registered, default managed), the `AndroidCodecs` self-test, and contract tests.
5. **PR 5: device validation and docs.**
   - ROG Phone 6D: `shadow` run through the opening and two more cutscenes, 0 mismatches.
   - Playback speed, 3 runs each with 2 and 3 threads, phone on charger, 36-38 °C, with an 「期望 → 真结果」 table.
     Rule: use the smallest thread count whose median is ≥ 0.97x. If 3 threads fall short, the owner decides
     before merge.
   - `docs/坑记录.md`, `compat/改动清单.md`.

## Risks & Mitigations

| Risk | Detection | Mitigation |
|---|---|---|
| Guest-controlled bytes reach libvpx in-process | fuzz driver (ASan) | step 0 version check; size, probability and MV-ref pre-checks |
| Patch drifts with a libvpx upgrade | patch apply fails; identity and parity tests | patch bytes in both recipe keys |
| C# and libvpx disagree (a bug in either) | parity; `shadow` | C# is the reference for Goal 2; a divergence blocks native for that feature until understood |
| Slot map drifts from guest memory | `shadow` in testing | slots invalidated on every write; any C# job resets the slots |
| Hooked path slower than the Phase 1 bench | step 1 hooked bench | stop rule in step 1 |
| A native regression on Android release cannot be switched off | device validation before default-on | accepted (Non-Goals); a fix ships as a new build |

## Alternatives Considered

- **Writing a real compressed header:** it cannot set the UV-mode and some MV tables, and needs a bool encoder.
  Rejected.
- **`VP8_SET_REFERENCE` imports:** reach only slots 0-2. Rejected; native resumes at key frames.
- **libvpx's own previous-frame MVs instead of importing the guest's:** they diverge from the guest buffer after
  any C# job, so they are not used. (Phase 1 showed the read buffer is the previous write buffer, not that libvpx's
  MVs equal it.)
- **Zeroing intra-block MVs at export instead of clearing `cm->mip`:** it also has to handle the unused `mv[1]` of
  single-reference blocks and any other stale field. Clearing the whole array matches C# at one place.

## Open questions for the owner

1. **Apple default.** Apple arm64 registers the backend but stays `managed` until a `shadow` run of an adapting
   title (GRID Autosport) passes on a Mac. Turning it on before that is the owner's call. Android defaults to
   native after PR 5.
2. **Thread count.** Decided by the PR 5 rule; the owner decides only if 3 threads fall short.
