# Goldfish review: design_phase2_B.md (rev 3)

* **Reviewer:** zero-context Goldfish (`egm-implementation-readiness`)
* **Date:** 2026-11-09
* **Target:** `docs/设计_原生VP9/design_phase2_B.md` rev 3 (cited as `B:<line>`)
* **Checked against:** libvpx 1.15.0 (scratchpad copy) and `perf/vp9-phase1` at `193f1f5`
* **Scope:** status of N1-N14 from `phase2B_rev2_report.md`, plus new blocking defects from the rev 3 edits

## 1. Status of N1-N14

| ID | Status | Reason |
|---|---|---|
| N1 | Resolved | B:219-223 checks only `CoefProbs[...][2]` on reachable contexts. Confirmed: `PIVOT_NODE 2` (`vp9_entropy.h:144`), `BAND_COEFF_CONTEXTS` gives 3 for band 0 (`:108`), and both libvpx (`vp9_detokenize.c:186`) and C# (`Nvdec.Vp9/Detokenize.cs:114`) index `pareto8_full[p - 1]` on that node. The census line "rejected by the pre-check = 0" is at B:499. |
| N2 | Partly | The memory map (B:438-452) is correct. `GetBlockLinearSize` for 1920x1088 gives luma 30 GOBs × 68 blocks × 1 KiB = 0x1FE0 units (≤ 0x2400) and chroma 0xFF0 units, so luma + chroma = 0x33F0 (≤ 0x4000). Surfaces end at 112 MiB, inside 128 MiB. But B:500 still says "one flat **64 MiB** buffer (offsets as above)". That buffer cannot hold surfaces placed at 64 MiB and above (D3). |
| N3 | Resolved | `update_error_state(vpx_codec_alg_priv_t *, const struct vpx_internal_error_info *)` is static in `vp9_dx_iface.c:195-201` and sets `base.err_detail`. `ctx->err` is a public field of `vpx_codec_ctx_t`, so the entry point can set it. `vpx_codec_error_detail` then returns `priv->err_detail` (`vpx_codec.c:59-63`). Small gap: when `init_decoder` fails, B:229 does not set `ctx->err`, so the error string is NULL. That is harmless. |
| N4 | Partly | `hdr.c`, the C slot mirror, MV chaining, the start at a key frame and the per-job bar are defined (B:541-547). But `android/codecs/vpxbench/build.sh` builds its own unpatched libvpx (its key hashes only `$0`) and compiles only `vpxbench.c` + `s2a_vp9.c`. §7 does not say to apply the patch there, add the patch to that key, or compile `hdr.c`. Without that, `s2a_vp9_nvdec` cannot link (D4). |
| N5 | Open | The resize driver cannot work as specified (D1). The fixed-filter literals are now conditional: resolved. |
| N6 | Resolved | The export struct, callback and setter are declared (B:154-174). It lands in PR 2 (B:151). The variant and the dylib come from `build.sh` (B:485). `last_frame_type` is read correctly: libvpx sets it at the start of `read_uncompressed_header` (`vp9_decodeframe.c:2648`), so at export time it still holds the previous frame's type. |
| N7 | Resolved | `S2A_VPX_VARIANT=ship\|nopatch\|export`, with the variant and patch bytes hashed into `H1`. The configure flags are otherwise shared. This fits the existing `H1`/`OUT`/`s2a-$K` layout of `services/vpx/build.sh`. |
| N8 | Resolved | `binary_ascii_globs` exists in `tools/public_repo_scan_rules.json:480` and is matched at `public_repo_scan.py:141`. The 1.5 MB total is under `MAX_BYTES` (16 MiB). |
| N9 | Resolved | `ModeOverride` and `ForceManaged` (B:353-356); one context, cache and memory per path (B:500). |
| N10 | Resolved, with a new defect | `InternalsVisibleTo` for three assemblies, `t_vp9_fuzz.sh` with `门禁手动`, and the environment-gated branch are fixed. But the `门禁覆盖` lines use commas (D5). |
| N11 | Resolved | Three ready-to-call units, generated on the Mac, base64, MV chaining and FNV hashes (B:424). |
| N12 | Partly | Routing catches the exceptions on every platform (B:340-342), and §5 drops the wrapper. But PR 4 (B:552-553) still lists "the Android failure wrapper". |
| N13 | Resolved | `cm->frame_refs[0..2].idx` (B:161, B:457). `RefBuffer.idx` exists and is used at `vp9_decodeframe.c:1563`. |
| N14 | Resolved, with a new defect | The symbol tuple, the absolute recipe-key paths, per-call state, shadow rules, no cache rents and `VpxVp9Backend.Threads` are all fixed. The `codec_shim` reorder misses a second caller (D2). |

## 2. New blocking defects from the rev 3 edits

### D1 (Blocker, PR 1 and PR 2). The resize encoder settings are rejected by libvpx

**Scope:** B:514-518, the census at B:521-529.

**Evidence:** `encoder_set_config` returns "Cannot change width or height after initialization" when
`g_lag_in_frames > 1` or the encode is not one-pass (`vp9/vp9_cx_iface.c:808-810`). B:516 asks for
"auto-alt-ref on with lag 16". The `vpx_codec_enc_config_set` call at frame 24 therefore fails, and
`adapt_720.ivf` cannot be made. `test/resize_test.cc` uses lag 0.

**Fix:**
- Encode `adapt_720.ivf` one-pass with `g_lag_in_frames` 0. Shrinking 1280x720 to 960x544 is within
  `valid_ref_frame_size` and below the initial size, so libvpx forces no key frame and the scaled references
  are kept (`:818-826`).
- With lag 0, this clip has no alt-ref and so no hidden frames. Say that the census is the union over both
  streams, with hidden frames coming from `fp_ragged.ivf`, or name the stream that must contain each feature.
- Also fix B:512, which says all streams are made "with ffmpeg + libvpx-vp9". State how `resize_enc.c` gets
  its raw frames, for example ffmpeg `testsrc2` piped as I420.

### D2 (Blocker, PR 1). The `codec_shim` reorder breaks the Opus recipe

**Scope:** B:487.

**Evidence:** `android/codecs/opus-build.sh:23` calls
`codec_shim s2a_opus "$target/libopus.a" "$repo_dir/services/opus/s2a_opus.c" "$target/include" "$target"`
in the old order. With the new signature `name archive include parent source...`, Opus would compile its include
directory as a source file, and the Android build fails.

**Fix:** list `android/codecs/opus-build.sh` in §7 with its call rewritten to the new order. Alternatively, keep
the old positional order and add extra sources at the end.

### D3 (Major, PR 3). The parity harness buffer size contradicts the memory map

**Scope:** B:500 compared with B:438-439.

**Fix:** change B:500 to "one flat 128 MiB buffer (§6 memory map)".

### D4 (Blocker, PR 1). The hooked bench has no patched libvpx

**Scope:** B:492, B:541-547. See N4.

**Fix:** add a row for `android/codecs/vpxbench/build.sh` in §7:
- apply the patch;
- hash the patch into its `codec_recipe_key` (`<(cat "$0" "$repo_dir/services/vpx/patches/0001-s2a-vp9-nvdec-hook.patch")`);
- compile `hdr.c` and the slot mirror next to `vpxbench.c`.

Also say how the bench computes `use_prev_frame_mvs` from the capture flags (C#'s `UsePrevInFindMvRefs` rule).

### D5 (Major, PR 2 and PR 3). The `门禁覆盖` globs are comma-separated

**Scope:** B:499, B:507-508.

**Evidence:** `tools/gate_route.py:129-130` splits the header on whitespace and matches each token with
`fnmatch`. In `services/vpx/**,`, the trailing comma becomes part of the pattern, so every glob except the
last one on each line never matches. The tests would then not run when those files change. Existing headers use
spaces (`tests/addr_need_test.sh:3`).

**Fix:** separate the globs with spaces.

### Non-blocking

- **PR 4 wrapper:** B:552-553 still names the Android failure wrapper (N12). Remove it.
- **Tests table:** the paragraph at B:505-508 splits the §8 tests table, so rows B:509-510 render outside it.
- **ASan build:** the fuzz build's "export variant plus `-fsanitize=address`" (B:503) has no mechanism in
  `build.sh`. This is a manual test, so it can be decided in PR 3.

## Verdict

- PR 1: **Not ready: D1, D2, D4**
- PRs 2-5: **Not ready: D1 (PR 2), D3, D5 (PR 2/3), N12 text (PR 4)**. PR 5 has no blockers of its own.

All are document edits. No new evidence or design change is needed.
