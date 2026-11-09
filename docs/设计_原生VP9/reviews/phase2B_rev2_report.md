# Goldfish review: design_phase2_B.md (rev 2)

* **Reviewer:** zero-context Goldfish (personas `egm-zoom-out`, `egm-design`, `egm-implementation-readiness`)
* **Date:** 2026-11-09
* **Target:** `docs/设计_原生VP9/design_phase2_B.md` rev 2 (cited as `B:<line>`)
* **Checked against:** libvpx 1.15.0 (scratchpad copy; `dec/` = `vp9/decoder/`, `com/` = `vp9/common/`), branch
  `perf/vp9-phase1` at `193f1f5` (`Nvdec/` = `compat/tree/src/Ryujinx.Graphics.Nvdec/`, `Vp9/` =
  `compat/tree/src/Ryujinx.Graphics.Nvdec.Vp9/`, `Video/` = `compat/tree/src/Ryujinx.Graphics.Video/`), and the build,
  gate and list files named in §7 and §8.
* **Standards:** implementation-readiness-standard, zoom-out-standard

## Verdict

**PR 1 cannot start as written.** One new finding (N1) makes the shipped hook reject every job, and PR 1's own
hooked speed check (B:454-456) would fail on it. PR 1 also leaves three choices to the implementer: how the
hooked bench builds frames (N4), how the unpatched libvpx for the identity test is built (N7), and whether the
synthetic `.ivf` files pass the public-repo scan (N8). All of these are document edits. None needs new evidence
or a different design.

Later PRs:

| PR | Can start once the document is fixed? | Blocking findings |
|---|---|---|
| 1 | no | N1, N4, N7, N8; N5 changes `make.sh` |
| 2 | no | N2 (offset spacing), N6 (export interface), N5 (census can fail by construction) |
| 3 | no | N3 (error string), N9 (mode seam for the harness), N10 (test access and the manual test name) |
| 4 | no | N11 (self-test fixture format), N12 (exception handling outside Android) |
| 5 | yes | none |

The rev 1 findings are mostly resolved (table below). The overall design still holds, and the libvpx-level claims
in the changed sections check out against the source, with the exceptions below.

## 1. Status of rev 1 findings

### phase2B_critic_report.md

| ID | Status | Reason |
|---|---|---|
| C1 interp filter | Resolved | B:228 writes the literal unchanged and the switchable bit for 4. The header-writer test covers literals 0..4 (B:424). |
| H1 stale intra MVs | Resolved | memset of `cm->mip` (B:190). Checked: nothing earlier in the frame lives in `mip`. The one non-zero "don't care" value, `mv[1] = 0x80008000` on single-ref sub8x8 NEAREST/NEAR blocks (`dec/vp9_decodemv.c:752-755`), is produced identically by C# (`Vp9/DecodeMv.cs:960-963`). |
| H2 entry point | Resolved, with a new defect | `init_decoder`, one `vp9_receive_compressed_data`, no superframe parse, `need_resync` set by hand (B:178-183) are all correct. The error string is lost, though (N3). |
| H3 jobgen offsets | Partly | The per-`RefCntBuffer` model matches libvpx (`FRAME_BUFFERS` = 12, `com/vp9_onyxc_int.h:41`; `get_free_fb` takes `ref_count == 0`, `:331-346`), and the derivation table is there. But the spacing is too small for any real frame size (N2), and `cm->ref_frame_idx` does not exist (N13). |
| M1 intra-frame ref checks | Resolved | "Inter frames only" (B:192). |
| M2 delta-Q encoding | Resolved | B:213, B:232, B:234. |
| M3 segment-map exactness | Resolved | `TrackSegmentMap` (B:295-297). It can be implemented from `LoadSegmentMap` (`Vp9/Decoder.cs:181-207`). |
| M4 "1 = nothing changed" | Resolved | Codes 1, 2 and -1 (B:157-160); 1 only from the pre-checks (B:169). |
| M5 stream coverage | Partly | 1280x720 with 4 tile columns, a ragged size and a census were added. But the resize method gives no scaled references, and two fixed-filter literals may never be produced (N5). |
| M6 coverage measurement | Resolved | Goal 4, harness report (B:423), key-frame interval (B:66), LRU rule (B:255-258). |

### phase2B_readiness_report.md

| ID | Status | Reason |
|---|---|---|
| C1 MV stale bytes | Resolved | Same as critic H1. |
| C2 routing exception | Resolved | B:289. |
| H1 interp filter | Resolved | |
| H2 probability clamping | Resolved, with a new defect | No clamping, but the replacement check rejects every frame (N1). |
| H3 interfaces | Resolved | The C# and C signatures are written out (B:132-163, B:312-348). The error string does not work (N3). |
| H4 step order and lists | Resolved | Everything is in PR 1 (B:406-415). Small wording issue in N14. |
| H5 test naming and wiring | Partly | Names, `contrib_tools.tsv` rows and the md5 driver are given. Open: how the unpatched build is made (N7), the manual fuzz test is named `*_test.sh` (N10), and test access to internals (N10). |
| H6 jobgen spec | Partly | Export is limited to the tool build and the driver model is defined. The export interface is undefined (N6), and the offsets overlap (N2). |
| H7 scope | Resolved | Android first; Apple arm64 registered but `managed`; x86 never registers (B:36-38, B:362-363). |
| M1 hook contract | Resolved | |
| M2 MV ref validation | Resolved | B:174. C# truncates the int to sbyte (`Vp9/Types/Vp9Common.cs:1084`), so routing out-of-range refs to C# is exact. |
| M3 cache key level | Resolved | The patch goes into `H1` (B:411). `H1` names the `libvpx.a` cache directory (`services/vpx/build.sh:13,19`). |
| M4 device self-test | Partly | Added (B:364), but the fixture cannot be produced or consumed as described (N11). |
| M5 fuzz recipe | Resolved | C driver, ASan, 10^4 mutations, fixed seed (B:426). The naming conflicts with the gate (N10). |
| M6 early speed check | Partly | Moved into PR 1 with a stop rule, but the bench's input path is undefined (N4). Decision rules for step 5 are given. |
| M7 PR boundaries | Resolved | B:448-468. |
| M8 harness memory model | Partly | Flat buffer, `IVirtualMemoryManager` and `InternalsVisibleTo` are given (B:423). The offset layout does not fit (N2), and there is no way to run both paths in one process (N9). |
| M9 libvpx version | Resolved | Step 0 (B:450-452). |
| M10 release-build switch | Resolved | Accepted under Non-Goals (B:96-97). |

## 2. New findings, ranked

### N1 (Blocker, PR 1). The "every probability byte is non-zero" pre-check rejects every frame

**Scope:** B:172-173 (pre-check), B:165 (no padding).

**Evidence:**
- VP9 coefficient band 0 has only 3 contexts: `BAND_COEFF_CONTEXTS(band) ((band) == 0 ? 3 : COEFF_CONTEXTS)`
  (`com/vp9_entropy.h:108`). The table type still has 6 (`vp9_coeff_probs_model`, `com/vp9_entropy.h:150`). The
  default tables list only 3 rows for band 0 (`com/vp9_entropy.c:370-376`), so contexts 3-5 are 0. The compressed
  header never updates them.
- So `cm->fc->coef_probs[tx][plane][ref][0][3..5][*]` is 0 on every frame. That is 4×2×2×3×3 = 144 bytes.
  `EntropyProbs.Convert` copies all 6 band-0 contexts from the guest (`Nvdec/Types/Vp9/EntropyProbs.cs:162-192`).
- The job generator fills `EntropyProbs` from `*cm->fc` (B:389), so **every generated job** carries those zeros.
  A guest driver built from libvpx tables will carry them too.
- `SegTreeProb` and `SegPredProb` are also 0 in libvpx until a frame codes a segmentation map. The design says
  they are unused (B:206-207), but the check still covers them.
- B:165 states that Vp9EntropyProbs has no padding, so "except padding" (B:172) excludes nothing.

**Result:** every job returns 1. Goal 4 (≥ 99% native) reads 0%. PR 1's hooked bench (B:455) would decode nothing.

**What zero actually does:** prob 0 and prob 1 give the same split in both bool decoders. The only place 0 is
unsafe is the Pareto lookup on the coefficient pivot node. libvpx indexes `vp9_pareto8_full[p - 1]`, and so does
C# (rev 1 critic L2). For libvpx that is an out-of-bounds read, so this case must stay a pre-check.

**Fix:** replace the rule with "for each tx size, plane and ref, band 0 contexts 0-2 and bands 1-5 contexts 0-5:
`CoefProbs[...][2]` (the pivot node) is non-zero". Alternatively, check all three nodes of the reachable
contexts only. Exclude `SegTreeProb`, `SegPredProb` and the unreachable band-0 contexts by name. Add a jobgen
census line "jobs rejected by the probability pre-check", required to be 0 on the synthetic streams.

### N2 (Blocker, PR 2 and PR 3). The job generator's surface offsets overlap

**Scope:** B:374-378, and the parity harness "one flat 64 MiB buffer (offsets as above)" (B:423).

**Evidence:**
- Offsets are in 256-byte units. Buffer `b` sits at `0x10000 + b*0x1000`: 1 MiB apart. Chroma is at luma
  `+ 0x800`: 512 KiB after luma.
- A 1280x720 luma plane is at least 921,600 bytes, more after block-linear alignment. A 1920x1080 luma plane is
  about 2.2 MB, and a whole 1080p surface about 3.3 MB.
- So `SurfaceWriter` writes luma over chroma and over the next buffer, on both synthetic streams and on every
  P4G frame.
- Native references live inside libvpx, while C# reads guest memory after a cache miss
  (`Nvdec/Image/SurfaceCache.cs:64-77,85-99`). The forced-fallback case (B:423) therefore compares C# reading
  corrupted references against native. The test measures the harness, not the decoder.
- Also undefined: where the harness places `PictureInfo` (`SetDrvPicSetupOffset`), the probability table and the
  tile data (`SetInBufBaseOffset`).

**Fix:**
- Space buffers 0x4000 units (4 MiB) apart, with chroma at luma `+ 0x2400`.
- Make the flat buffer 128 MiB: 16 MiB base + 12 × 4 MiB = 64 MiB, which reaches the end of a 64 MiB buffer.
- Write down offsets for PictureInfo, probabilities, tile data, the two MV buffers and the counts buffer.
- Add a harness assert that no two live surfaces overlap: the computed block-linear size must be smaller than the
  spacing.

### N3 (Major, PR 3). The error string is always NULL

**Scope:** B:285 (log "libvpx's error string"), B:343-344 (`error: vpx_codec_error_detail or NULL`), B:178-183.

**Evidence:**
- `vpx_codec_error_detail` returns a string only when `ctx->err != 0` (`vpx/src/vpx_codec.c:59-63`).
- `ctx->err` is set by the `vpx_codec_decode` wrapper. `priv->err_detail` is set by `update_error_state`, which
  only `decode_one` calls (`vp9/vp9_dx_iface.c:195-201,327-332`).
- The new entry point calls `vp9_receive_compressed_data` directly (B:180), so neither is updated, and the shim
  always returns NULL. The `【红】` line for -1 then has no reason.

**Fix:** in step 4 of the entry point, on failure:
- call `update_error_state(ctx_priv, &pbi->common.error)`;
- set `ctx->err` to the returned code;
- clear `ctx->err` on success.

Alternatively, have `s2a_vp9_nvdec_decode` return `pbi->common.error.detail` through an out parameter. Also say
which code an `init_decoder` failure returns (-1 is the obvious choice). At that point `pbi` is NULL, so step 4's
`need_resync` write must be skipped.

### N4 (Major, PR 1). The hooked bench has no defined way to turn job records into frames

**Scope:** B:454-456.

**Evidence:**
- The 1033 S2V9 captures hold `PictureInfo`, `EntropyProbs`, registers and tile data. They hold no uncompressed
  header and no MV buffer contents (`Nvdec/S2AVp9Capture.cs:16-20`).
- To call the hook, the bench must write a VP9 uncompressed header. The only header writer is C#'s, and it
  arrives in PR 3.
- The other route is the real movie-37 IVF frames. But their header says `frame_parallel_decoding_mode` 1, so
  libvpx does not count, and the bench would skip the very cost it is meant to measure (B:455).
- MVs have to come from the previous hooked job. That only works if the run starts at a key frame and is
  sequential.

**Fix:** pick one route and state it. For example:
- (a) PR 1 adds a ~100-line C header writer in `vpxbench` that mirrors §2. The C# writer in PR 3 is then tested
  against it on the synthetic streams, which also gives the header-writer test a second implementation.
- (b) The bench takes real IVF frames, patches the `frame_parallel_decoding_mode` bit in place, and passes
  probabilities from the matching capture.

Either way, state that the run starts at capture job 0 (a key frame) and chains MVs. The bar should also be per
job: Phase 1's 5.2 ms is per IVF packet, and a packet can hold two jobs, so "≤ 8 ms per job" is looser than it
reads.

### N5 (Major, PR 1 and PR 2). Two required census features cannot be produced as specified

**Scope:** B:432-445.

**Evidence:**
- **Resize:** "two encodes concatenated at the IVF level" makes the second half start with a key frame at
  960x544. No inter frame then references a 1280x720 picture, so the census line "a resize with scaled references"
  is 0, and `vp9_jobgen_test.sh` fails by its own rule (B:436).
- **Fixed filters:** libvpx's encoder has no public control that forces a frame-level interpolation filter. It
  chooses one from statistics. Whether `testsrc2` or `mandelbrot` produce literal 0 (smooth) and literal 2
  (sharp) is not established. If they do not, the census fails with no remedy.

**Fix:**
- Make the resize with a real mid-stream size change, without a key frame. Options are a small C encoder driver
  that calls `vpx_codec_enc_config_set` with a new `g_w`/`g_h` mid-stream (the pattern of libvpx's
  `test/resize_test.cc`), or `vpxenc` dynamic resize. Commit the driver next to `make.sh`.
- Make "each fixed literal" conditional on the encode containing it, since the header-writer test already covers
  literals 0..4. Or name the encoder settings that were checked to produce them.
- Record the actual census in the README.

### N6 (Major, PR 2 and PR 3). The `S2A_VP9_EXPORT` interface is undefined

**Scope:** B:126-128, B:380-390, B:424.

**Evidence:** the generator reads `*cm->fc` "right after `read_compressed_header`", `cm` fields "after the
header", and libvpx state "before this frame updates them", all "through the export hook". The header-writer
test calls "the patched libvpx (export build)" from a csproj. But no callback, struct, exported symbol or
dylib is specified. It is also unclear whether the export code lands in PR 1's patch or in PR 2.

**Fix:** declare the export API in the patch header under `#ifdef S2A_VP9_EXPORT`. For example, a callback
`void (*s2a_vp9_export_cb)(const s2a_vp9_export *e, void *user)` fired once per frame after
`read_compressed_header`. `s2a_vp9_export` is a plain struct of the fields in the §6 table, so the generator
does not depend on `VP9_COMMON` layout. Say which build target produces a Mac dylib for the C# test, where it is
cached, and that the export code ships in PR 2's patch revision. The patch file is an input to both recipe keys,
so changing it in PR 2 is fine.

### N7 (Major, PR 1). How the unpatched libvpx is built is left open

**Scope:** B:421.

**Evidence:** only a cache path is given (`$S2A_BUILD_CACHE/codecs/vpx-identity-<sha>`). `services/vpx/build.sh`
has no "skip patch" mode. If the test runs its own `configure`, its flags can drift from `build.sh:44-46`, and
the test then compares two different configurations.

**Fix:** add an option to `services/vpx/build.sh`, for example `S2A_VPX_PATCH=0`, that skips `patch -p1` and
adds `nopatch` to `H1`. The identity test builds both through it, and the jobgen build in N6 uses the same
mechanism with `-DS2A_VP9_EXPORT`. Add `# 门禁预算：` if a cold build of two libvpx copies can exceed 600 s on
the gate host.

### N8 (Major, PR 1). The synthetic `.ivf` files are not allowed by the public-repo scan

**Scope:** B:430.

**Evidence:**
- `tools/public_repo_scan.py:141-172` treats a binary that matches no rule as `unreviewed_type`, and a changed
  file of that type makes the scan return 2 (`:433`).
- `tools/public_repo_scan_rules.json` `binary_ascii_globs` lists `services/S2A.Services.Tests/data/*.ivf` but
  nothing under `tests/data/`.

**Fix:** add `tests/data/vp9_synth/*.ivf` to `binary_ascii_globs` in PR 1, and list the rules file in §7.

### N9 (Major, PR 3). The parity harness cannot run both paths in one process

**Scope:** B:299-300 ("read once per process"), B:423.

**Evidence:** the harness replays every record through "the C# path and the native path, using
`Vp9Decoder.Decode`". It also forces job N to C# in the forced-fallback case. With the mode fixed per process
by an environment variable and no other seam, the implementer has to invent one: a static override, a
constructor parameter, or two processes.

**Fix:** define an internal seam, for example `internal static string ModeOverride` and
`internal static Func<int, bool> ForceManaged` on `Vp9Decoder`, used only through `InternalsVisibleTo`. State
that the harness uses one `NvdecDecoderContext`, `SurfaceCache` and flat memory per path.

### N10 (Major, PR 3). Test access to internals, and the manual fuzz test name

**Scope:** B:424-426.

**Evidence:**
- `Vp9HeaderWriter` and `Vp9SlotMap` are internal (B:211, B:246). Only `Vp9NativeParity` gets
  `InternalsVisibleTo` (B:423). The header-writer and slot-map csproj tests cannot reach the types.
- `tools/gate.sh:106` runs every `tests/*_test.sh`. A file named `tests/vp9_fuzz_test.sh` is therefore a gate test,
  not a manual one.
- Manual tests are marked with a `门禁手动：<原因>` header (`tools/test_wired_lint.py:7`). The repo's manual
  tests are named outside the `*_test.sh` pattern (`tests/t_save_transfer_real.sh`).
- "Whitelisted as manual" for one mode of `vp9_jobgen_test.sh` (B:422) has no mechanism, because the whitelist is
  per file.
- None of the new tests has a `# 门禁覆盖：` line. The router uses it to decide which tests a change triggers.

**Fix:**
- Add `InternalsVisibleTo` for `Vp9HeaderWriterTest` and `Vp9SlotMapTest` in the same compat patch. There is
  precedent: `compat/patches/0002-hle-internals-visible.patch`.
- Rename the fuzz script to `tests/t_vp9_fuzz.sh` with `# 门禁手动：分钟级 ASan 模糊测试`.
- Say that the jobgen local mode is an environment-gated branch inside the gate test, not a whitelist entry.
- Give each new test its `门禁覆盖` globs: `services/vpx/**` and the patch for the libvpx tests, and
  `compat/tree/src/Ryujinx.Graphics.Nvdec/*Vp9*` and the `Video` files for the C# ones.

### N11 (Major, PR 4). The device self-test fixture cannot be built or used as described

**Scope:** B:364.

**Evidence:** "a 3-job S2V9 fixture … decoded through `s2a_vp9_nvdec`". S2V9 records hold no uncompressed header
(see N4). `AndroidCodecs.cs` calls the C shim directly, as for VP8 (`android/cs/aot/AndroidCodecs.cs:21-23`), and
has no access to the internal C# header writer. The fixture format, its generator and its size are not given.

**Fix:**
- Define the fixture as three ready-to-call units, each holding:
  - frame bytes (header + 1 byte + tile data);
  - the 3087 probability bytes;
  - the `Vp9NativeParams` ints.
- Generate it on the Mac from `fp_ragged.ivf` (key frame plus 2 inter frames) with the PR 3 header writer. Embed
  it as base64 like `Vp8Frames` (`AndroidCodecs.cs:32`).
- Chain MVs inside the test: job k's `prev_mvs` is job k-1's output.

### N12 (Medium, PR 3 and PR 4). Backend exceptions on non-Android hosts

**Scope:** B:287-288.

**Evidence:** "The backend is disabled process-wide (the Android wrapper does this)." On Apple there is no
wrapper (B:363), so a `DllNotFoundException` from `VpxVp9Backend` reaches `Vp9Decoder.Decode` on the Nvdec device
thread. Apple's default is `managed`, but `S2A_NVDEC_VP9=native` triggers it. On Android it is also unclear what
the wrapper returns: if it returns `Error`, routing logs a second `【红】` per context, and "process-wide" needs a
static flag that is not described.

**Fix:** catch the three exception types in `Vp9Decoder` routing on every platform, set a static
`_nativeDisabled`, and log once. The Android wrapper then adds nothing for VP9 and can be dropped. That is a
simplification.

### N13 (Medium, PR 2). Wrong libvpx field names in the derivation table

**Scope:** B:383.

**Evidence:** `cm->ref_frame_idx` does not exist in the 1.15.0 decoder. The slot index is a local `ref` in
`read_uncompressed_header` (`dec/vp9_decodeframe.c:2740-2746`), and the stored value is the buffer index
`cm->frame_refs[i].idx`.

**Fix:** "Last/Golden/Alt offsets = buffers `cm->frame_refs[0..2].idx`". That is already the `RefCntBuffer`
index the offset model needs.

### N14 (Nit). Smaller items

- **Symbol list:** B:415 says "add the `s2a_vp9_*` exports to the symbol list". `link_symbols.py` has no export
  list. It collects definitions from the shim files (`android/core-link/link_symbols.py:845-850`). Adding
  `s2a_vp9.c` to that tuple is the whole change.
- **Recipe key path:** B:412 uses relative paths. The existing recipe hashes `"$repo_dir/android/codecs/vpx-build.sh"`
  (`android/codecs/vpx-build.sh:5`). Write
  `<(cat "$repo_dir/android/codecs/vpx-build.sh" "$repo_dir/services/vpx/patches/0001-s2a-vp9-nvdec-hook.patch")`.
- **`codec_shim` signature:** its source is the middle positional argument (`android/codecs/cache.sh`,
  `codec_shim name archive source include parent`). State the new argument order.
- **Census exclusion:** the "UsePrevInFindMvRefs disagreement" frames (B:396-398) do not need to leave Goal 2(a).
  Native uses C#'s value (B:147, B:191), so native and C# still agree. Excluding them only loses coverage.
- **Shadow mode and routing:** B:289 says any C# job leaves `NativeActive` false, but in `shadow` every job is
  decoded by C#. State that rule 3 does not apply in `shadow`. Also say that the picture hash covers visible rows
  only.
- **Native path and the cache:** say that the native path rents no surfaces from `rm.Cache`. Today's code rents
  all four before decoding (`Nvdec/Vp9Decoder.cs:29-32`).
- **Per-call state:** `pbi->s2a_unrepr` and `cm->s2a_prev_mvs` must be reset at the start of every hooked call.
  The decoder-owned `MV_REF` buffer must be freed in `vp9_decoder_remove`.
- **Thread count:** it is hard-coded as `s2a_vp9_open(2)` (B:353), but PR 5 may choose 3. Say where the value
  lives.

## 3. Claims verified as correct

- **`init_decoder` from inside `vp9_dx_iface.c`:** it is static in the same file (`vp9/vp9_dx_iface.c:257-295`).
  It applies `cfg.threads`, `row_mt` and `lpf_opt`, which `s2a_vp9_open` stores through `vpx_codec_dec_init` and
  `VP9D_SET_ROW_MT` (`services/vpx/s2a_vp9.c:10-27`). `vp9_receive_compressed_data` is reachable through
  `vp9_decoder.h`.
- **Bypassing `decode_one`:** this skips the superframe parse, the trailing-byte loop, `peek_si` and the
  `ctx->need_resync` bookkeeping. Only `decoder_get_frame` reads `ctx->need_resync` (`:423`), and the hook does
  not use it.
- **`need_resync`:**
  - `vp9_decoder_create` sets it to 1 (`dec/vp9_decoder.c:191`).
  - A key frame clears it and flushes all buffers (`dec/vp9_decodeframe.c:2702-2706`).
  - An inter frame with it set fails (`:2778-2783`).
  - `vp9_receive_compressed_data`'s error path does not set it (`dec/vp9_decoder.c:439-447`).

  So B:183's explicit `pbi->need_resync = 1` is required and sufficient, and routing's "resume only at a key
  frame" matches.
- **memset of `cm->mip` after `read_uncompressed_header`:**
  - `mip` is reallocated only inside `read_uncompressed_header` (`resize_context_buffers` →
    `vp9_alloc_context_buffers`, `com/vp9_alloccommon.c:134-137`).
  - `vp9_setup_past_independence` touches only `prev_mip`, which is NULL in the decoder
    (`com/vp9_entropymode.c:461-464`).
  - Nothing before tile decoding writes `mip`.
  - Stale `mi_grid` pointers (cleared only on resize, `com/vp9_alloccommon.c:182`) are read only for neighbours
    already decoded in this frame.

  Cost: about 35k entries × 52 bytes ≈ 1.8 MB per 1080p frame.
- **Error path from `read_compressed_header`:** `vpx_internal_error` longjmps to `vp9_receive_compressed_data`
  (`dec/vp9_decoder.c:439-447`). `hold_ref_buf` is already 1 (`dec/vp9_decodeframe.c:2817`), so
  `release_fb_on_decoder_exit` returns the held references and the new buffer. `last_frame_type` and
  `last_intra_only` are already updated (`:2648-2649`), and the counts are already zeroed. Returning 2 with a
  forced resync is the correct contract.
- **Frame buffers:** `FRAME_BUFFERS` = 12. `get_free_fb` takes the lowest index with `ref_count == 0`, and slot
  references hold a count, so a buffer is never reused while any slot holds it. A frame with
  `refresh_frame_flags == 0` releases its buffer at the next call (`dec/vp9_decoder.c:410-415`), so consecutive
  non-reference outputs may share an offset. That is harmless for parity.
- **Struct sizes** (computed from the field lists): `Video/Vp9EntropyProbs.cs` is 3087 bytes, and
  `Video/Vp9BackwardUpdates.cs` is 3302 uints = 13208 bytes. `Vp9MvRef` is 16 bytes.
- **MV export and import:**
  - libvpx writes `cur_frame->mvs` only on inter frames (`dec/vp9_decodemv.c:819-845`), and C# `GetMvs` clears
    the buffer on intra frames (`Vp9/Types/Vp9Common.cs:1089-1095`). That matches B:195.
  - `prev_frame_mvs` is at `dec/vp9_decodemv.c:492-495`, and its strides match C#'s `miCols`
    (`Nvdec/Vp9Decoder.cs:118-124`).
- **Line references:** `read_compressed_header :2866`, `use_prev_frame_mvs :2986`, adaptation `:3051` (B says
  3050), per-tile counts `:2390-2395` / `:2434-2439`.
- **C# side:**
  - `LoadSegmentMap` bookkeeping is as described in 4(c) (`Vp9/Decoder.cs:181-207`).
  - `SurfaceCache.Get` treats offset 0 as "do not read" (`Nvdec/Image/SurfaceCache.cs:71,90`), so `Invalidate`
    must clear the owner and size, as B:360 says.
  - `NvdecDevice.cs:77` and `VpxSurface` (`Nvdec/Vpx/VpxVp8Decoder.cs:103`) exist.
  - `ResourceManager(DeviceMemoryManager, SurfaceCache)` and `DeviceMemoryManager.Map` make the harness memory
    feasible.
- **Build and gate:**
  - `services/vpx/build.sh` copies `src/vpx/` into the include directory (`:48`), and `vpx-build.sh` does the same
    (`:19`), so a new `vpx/s2a_vp9_nvdec.h` ships.
  - The four list files are at the cited places.
  - `build-essential` in the Docker image brings `patch`.
  - `contrib_tools.tsv` uses the tool names `Xcode` and `.NET 10 SDK`.

## 4. Zoom-out

- **Approach:** still the smallest one that meets Goals 1-3. Rev 2 removed the rev 1 over-reach (export mode in
  shipped libraries, the routing exception).
- **Simplification available:** N12. Handling backend exceptions in routing makes the Android VP9 wrapper
  unnecessary.
- **YAGNI watch:** Apple registration with default `managed` (B:363) adds an untested code path on Apple until the
  GRID check. It is acceptable because the change is one line and gated by the owner decision.
- **Main risk:** N1 and N2 are the kind of defect that would make the first parity run fail for harness reasons
  and push the implementer into ad-hoc fixes. Fix both in the document before PR 1.
