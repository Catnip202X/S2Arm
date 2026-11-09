# Goldfish review: design_phase2_B.md (rev 1). Zoom-out and implementation readiness

* **Reviewer:** zero-context Goldfish (personas `egm-zoom-out` and `egm-implementation-readiness`)
* **Date:** 2026-11-09
* **Inputs:** `design_phase2_B.md`, `phase1_results.md`, and the parent `design_draft.md` rev 3, which is read
  from commit `b800693` because it is not on this branch (see L1). Code checked: branch `perf/vp9-phase1` and the
  libvpx 1.15.0 source from the pinned tarball (sha256 `e935eded…`).

## Verdict

**Not ready to start.** The overall approach is right. Phase 1 rules out K and 0, and a small libvpx patch with
C# fallback is the least code that meets Goals 1 to 3. However, two findings break the plan's own correctness
goals (C1, C2), and several High findings leave interface or test decisions to the implementer. All of them can
be fixed by editing the document. None needs more evidence or a different design. Once C1, C2 and H1 to H7 are
fixed, step 1 can start.

## What was checked and is correct

- Every named file exists: `android/codecs/vpx-build.sh`, `android/codecs/cache.sh` (`codec_shim`, which takes a
  single source today), `services/vpx/build.sh`, `services/vpx/s2a_vp9.c`, the four list files at the cited lines
  (`link_symbols.py:845`, `cache-identity.py:27`, `test_gate_apk.py:448`, `test_doctor.py:38`),
  `android/cs/video/AndroidVideo.cs`, `platform/apple/cs/video/AppleVideo.cs`, `S2AVideoDecoders.cs`,
  `NvdecDecoderContext.cs`, `Image/SurfaceCache.cs`, and `NvdecDevice.cs:77` (`Vp9Decoder.Decode(_rm, …)`).
- The libvpx 1.15.0 line references are right:
  - `read_compressed_header` at `vp9_decodeframe.c:2866`;
  - `use_prev_frame_mvs` at `:2986`;
  - adaptation at `:3050`;
  - per-tile count init and accumulation at `:2391` and `:2435`. These are in `decode_tiles_row_wise_mt`, so
    row-MT does collect counts;
  - `prev_frame_mvs` at `vp9_decodemv.c:493` and `vp9_mvref_common.c:26`.
- libvpx forces `refresh_frame_flags` to 0xFF on key frames.
- The key-frame table pre-check is justified. The C# decoder reads the guest's `KfYModeProb`, `KfUvModeProb` and
  `KfPartitionProb` (`DecodeMv.cs:1077,1139`, `Vp9Common.cs:369`), while libvpx uses constants.
- `Vp9MvRef` is 16 bytes (2 × int16 MV pairs + 2 × int32 refs).
- `Vp9BackwardUpdates` matches libvpx `FRAME_COUNTS` shape for shape. The guest layout `BackwardUpdates` is
  produced by existing code (`Types/Vp9/BackwardUpdates.cs`).
- The C# decoder always collects counts (`FrameParallelDecodingMode` is never set). This matches the header's
  `frame_parallel_decoding_mode` 0.

## Critical

### C1. Goal 2's "MV buffer byte-identical" fails on ordinary streams as designed

**Evidence.** Both decoders write `mi->mv[0..1]` and `mi->ref_frame[0..1]` into the MV buffer for every 8x8 of
an inter frame:
- libvpx does it at `vp9_decodemv.c:833-839`;
- C# does it at `DecodeMv.cs:1162-1170`.

For an intra block in an inter frame, neither decoder writes `mv`. libvpx's `read_intra_block_mode_info` only
sets `ref_frame`. For a single-reference NEWMV block, neither writes `mv[1]` (`assign_mv`, `vp9_decodemv.c:410`).
What lands in those fields is whatever the mode-info struct held before:
- libvpx allocates `cm->mip` once with `vpx_calloc` (`vp9_decoder.c:148`) and never clears it per frame. Only
  `mi_grid_base` is cleared (`vp9_dec_setup_mi`). So libvpx writes the previous frame's values.
- C# allocates `Mip` per frame from `MemoryAllocator`, which clears every allocation (`MemoryAllocator.cs`,
  `allocation.AsSpan().Clear()`). So C# writes zeros.

The parity gate (step 4) and `shadow` mode (step 7) will therefore report MV mismatches on the first inter frame
that has an intra block or a single-ref NEWMV block. That is nearly every frame. The implementer then has to
choose between zeroing libvpx state, masking the comparison, or changing C#, and the document does not say which.

**Fix.** Make it a stated patch rule. When the hook is set, `memset(cm->mip, 0, …)` before decoding, which mirrors
C#'s cleared allocation. One 1080p frame is about 32k `MODE_INFO`, a sub-millisecond memset. Alternatively, zero
`mv` for intra blocks and `mv[1]` for single-ref blocks at export.

Also define a masked comparison for diagnostics: compare `ref_frame[0..1]` always, and `mv[i]` only where
`ref_frame[i] > 0`. Report masked and unmasked results separately, so a "don't-care" difference is never
confused with a real one.

### C2. A routing exception lets C# → native happen at a non-key frame, against stale libvpx slots

**Evidence.** §4 item 1 says that after a C# job, "`nativeActive` becomes false unless the reason applied to this
frame only and the switch is exact". Item 5 says C# → native happens only at a key frame.

The exception names no members. If an implementer keeps native active after one C# frame (for example a single
segmentation frame), that C# frame writes guest offset X. But §3 invalidates slots only "after any native
decode". So a libvpx slot still maps X to the old picture, and the next native frame that references X decodes
against the wrong reference with no report. This breaks Goal 3.

**Fix.** Delete the exception. Any job decoded by C# turns `nativeActive` off and invalidates all slots.
Native resumes only at a key frame.

## High

### H1. The interpolation filter mapping is wrong

**Evidence.** `PictureInfo.InterpFilter` carries the bitstream literal, not the libvpx enum. The C# decoder
applies `LiteralToFilter` to it (`Nvdec.Vp9/Decoder.cs:23-26,77-80`; `Constants.cs`: EightTap 0, Smooth 1). The
§2 table says the value is "the same enum as libvpx" and is written as the "inverse of `literal_to_filter`". That
swaps regular and smooth on every frame with a fixed filter. libvpx's encoder does emit fixed-filter frames.

**Fix.** Write `PictureInfo.InterpFilter` unchanged as the 2-bit literal when it is less than 4. Write the
switchable bit when it equals 4. Add a header-writer test case for a fixed-filter frame.

### H2. "Probabilities clamped to 1..255" silently diverges from C#

**Evidence.** In the Risks table, the C# reference does not clamp. A zero probability is still well defined
arithmetic in both bool decoders: `split = 1 + (((range - 1) * prob) >> 8)` and libvpx's equivalent. Clamping
makes native output differ from C# with no report, which breaks Goals 2 and 3.

**Fix.** Do not clamp. If zero probabilities are considered unsafe, route the job to C# instead, with a single
`【红】`.

### H3. Interfaces are left open: factory type, `Decode` signature, shim signature, Android failure boundary

**Evidence:**
- §5 declares `Func<VpxVp9Decoder>` (a concrete public class) and `Decode(ref PictureInfo, ref Vp9EntropyProbs,
  ReadOnlySpan<byte> tiles, …)`, with an internal method and a literal "…". It also declares
  `s2a_vp9_nvdec(void *h, …)`, again with a literal "…".
- The Android registration must be "with the Android failure boundary (same shape as `AndroidVp8Decoder`)".
  `AndroidVp8Decoder` is a wrapper that implements the public `IVp8Decoder` interface. It cannot wrap an
  internal `Decode` on a sealed concrete class from `android/cs/video`, because the Nvdec csproj has no
  `InternalsVisibleTo`.
- `VideoDecoderFactoryContractTests` works with spies that implement the decoder interfaces. A concrete-class
  factory leaves no way to inject a spy.
- The parent required "exact interfaces" in this document.

**Fix.** Specify:
- (a) a decoder interface. Either it is internal plus `InternalsVisibleTo` for the Android and Apple video
  assemblies and the test projects, added through a compat patch, or it is public and takes byte spans and
  pointers;
- (b) the full C# method signature: inputs, the output surface, the MV write span, the counts destination, and a
  return enum covering 0, 1 and < 0;
- (c) the exact C prototype of `s2a_vp9_nvdec` and its `[LibraryImport]`;
- (d) who opens the libvpx handle and when (lazily on the first key frame?), and what "has not failed" means in
  routing (factory failure, `DllNotFound`, or a decode error).

### H4. The step order breaks the Android build, and the identity lists miss the patch file

**Evidence:**
- Step 4 adds `[LibraryImport("s2a_vpx")]` declarations for `s2a_vp9_*` to compat code that the Android build
  compiles. The Android shim list and recipe gain `s2a_vp9.c` only in step 5. `link_symbols.py` reports
  `s2a_*` imports in `CODEC_LIBS` that have no definition in the listed shims as `missing`
  (`link_symbols.py:845-875`). Between steps 4 and 5, Android is therefore red, against the rule that each step
  leaves the tree buildable.
- Separately, the patch file `services/vpx/patches/0001-….patch` becomes an input to both recipes, but the
  document only says to add `s2a_vp9.c` "wherever `s2a_vp8.c` is listed". `cache-identity.py` (`SHARED`),
  the `test_gate_apk.py:440-449` fixture and the `test_doctor.py:38` copy list also need the patch path, or the
  Docker cache identity goes stale and the fixtures fail to build.
- `codec_recipe_key` hashes exactly one file (`cache.sh:48-50`). The document does not say how the patch
  enters it.

**Fix:**
- Move the Android recipe and lists to step 1 or 2, or merge them into step 4.
- List the patch path in all four places.
- Specify `codec_recipe_key vpx "$vpx_sha" <(cat vpx-build.sh patch)`, or a multi-file signature.
- Specify how `codec_shim` hashes several sources plus the patch, and fix line 1 of `vpx-build.sh`.

### H5. The tests as named will not run in the gate, and several have no pass/fail rule

**Evidence:**
- The gate runs `tests/*_test.sh` only (`tools/gate.sh:106`). `tests/test_wired_test.sh` flags C# test projects
  that nothing calls. `tests/vpx_patch_identity.sh`, `tests/vp9_jobgen_check.sh` and `tests/vp9_native_parity`
  match neither rule.
- Existing VP9 tests follow `tests/<name>_test.sh` + `tests/<name>/*.csproj` with a `# 门禁覆盖：` header
  (`vp9_coef_counts_test.sh`). Tests that need Xcode or the .NET SDK need rows in `tools/contrib_tools.tsv`.
- Tests that need game files must be whitelisted as manual tests, with a reason.
- Undefined criteria:
  - Step 2 says the generated records match "the synthetic stream (in repo)", but nothing defines what they are
    compared against.
  - Step 3's header round-trip calls the native export hook from a C# unit test, but the document does not say
    which project or binary that runs in.
  - Step 1 needs "`vpxdec --md5`-equivalent" output, but both recipes build with `--disable-tools`, so there is no
    `vpxdec`. The test also needs a second, unpatched libvpx build, and neither its cache key nor its gate time
    budget is given.
- The synthetic stream is used from step 1, but no step creates it. It appears only in the "Synthetic stream"
  section and in the step 8 docs.

**Fix:**
- Name the tests `tests/vpx_patch_identity_test.sh`, `tests/vp9_jobgen_test.sh` and
  `tests/vp9_native_parity_test.sh` (+ `tests/vp9_native_parity/`), each with a `# 门禁覆盖：` header.
- Add the `contrib_tools.tsv` rows: `.NET 10 SDK,Xcode` for parity, and `Xcode` for the libvpx ones.
- Whitelist the local-only runs.
- Specify the small C driver that replaces `vpxdec --md5`, and say where the unpatched build is cached.
- Make "commit the synthetic stream and its generator script" part of step 1.
- State the expected values for step 2: the synthetic-stream records are self-checked by round-tripping the
  header through the export hook.

### H6. The job generator is underspecified, and its offset model cannot represent real streams

**Evidence:**
- `S2A_VP9_EXPORT` appears only as a name. It is not in `s2a_vp9_nvdec.h`, and its outputs, how it is switched
  on, and its lifetime are not given.
- "Slot i is offset `0x100000 * (i + 1)`" is static, but VP9 refreshes several slots at once. Every key frame
  refreshes all 8, so the next frame's Last, Golden and Alt all point at the key frame's single output. A static
  slot-to-offset map would make later references point at offsets that were never written.
- How each `PictureInfo` flag (`LastShowFrame`, `LastFrameIsKeyFrame`, `FrameSizeChanged`, `ErrorResilientMode`,
  `SurfaceParams` bit depth, `BitstreamSize`) is derived from libvpx state is not given, and those flags decide
  `UsePrevInFindMvRefs`.
- The parent called a whole-corpus generator a "guest-driver emulator, not in scope". Bringing it in is a scope
  change, and the 1033-job check (one movie, no intra-only frames or resizes) is its only validation.
- The export mode also ships in production libvpx on both platforms.

**Fix:**
- Define the generator's driver model: each frame's output gets a fresh offset from a pool, and each slot records
  the offset of the picture it holds. Then the guest's Last, Golden and Alt are the offsets of the slots named by
  `ref_frame_idx`.
- Give a field-by-field `PictureInfo` derivation table.
- Compile the export mode only into the tool's own libvpx build (`#ifdef S2A_VP9_EXPORT`, set by
  `tools/vp9_jobgen`'s build), so the shipped patch has one mode.

### H7. Scope regression: GRID Autosport dropped, Apple switched to native by default, x64 not excluded

**Evidence:**
- The parent's Goal 4 required that GRID Autosport, the one known adapting title, keeps decoding correctly. Its
  C# path had a green-screen bug fixed on 11-07 (`tests/vp9_coef_counts`). Phase 2B replaces it with a 64-frame
  synthetic stream, yet makes native the default on every host that registers a factory. On Apple, every VP9
  title, GRID included, would switch to native with no title-level check.
- `AppleVideo.Register` also runs on Intel Macs, and `services/vpx/build.sh` builds `osx-x64` with the generic C
  target. That contradicts the "x86 hosts" non-goal, and it is likely slower than C# with SSE/AVX.

**Fix:**
- Register the VP9 factory only when `RuntimeInformation.ProcessArchitecture == Arm64`.
- Add a step-7 item: a `shadow` run of GRID's opening on whichever host has the game, with no mismatch. If no
  host has it, ship native as the default only on Android first, and record the Apple decision as an owner
  choice.

## Medium

### M1. The hook's "1 = nothing changed" contract is false, and its checks are not limited to inter frames

**Evidence:**
- Any `vpx_internal_error` after `read_uncompressed_header` leaves libvpx state changed. `decode_one` sets
  `need_resync = 1` (`vp9_dx_iface.c:328-331`), and `last_frame_type`, `last_intra_only`, the size and the loop
  filter state are already updated. Routing turns native off until the next key frame, so this is safe in
  practice, but the header comment and the §1 table promise otherwise.
- The §1 row sets `reference_mode` and checks "compound must be allowed" with no frame-type condition. libvpx
  reads the reference mode only for non-intra frames. If the guest driver leaves a stale `ReferenceMode` on key
  frames, every key frame returns 1 and native never activates.

**Fix:**
- Document "1 = not representable; libvpx must be resynced at a key frame".
- Apply the reference-mode, compound and `tx_mode` lossless checks to inter frames only, matching libvpx's own
  reads.
- Name the error code. Reusing `VPX_CODEC_UNSUP_FEATURE` together with the `pbi` flag avoids changing a public
  enum.

### M2. Guest MV refs index libvpx arrays without validation

**Evidence.** In the Vp9MvRef → `MV_REF` conversion, the int32 to int8 cast is unchecked. libvpx then uses the
value as an index: `ref_sign_bias[prev_frame_mvs->ref_frame[0]]` (`vp9_decodemv.c:~585`), and the array has 4
entries. The values are guest-controlled.

**Fix.** During conversion, route the job to C# when any ref is outside -1..3. Add this case to the fuzz pass.

### M3. Apple cache keys: where the patch hash goes

**Evidence.** `services/vpx/build.sh` caches `libvpx.a` under `$OUT=…-$H1`, where `H1` hashes only the script
and the deployment targets. The shim key `K` hashes the shims, and release jobs use prebuilt libraries looked up
by `KEY` (`s2a_prebuilt_lib`). "Hash it into the key" is ambiguous. If the patch is hashed into `K` only, the
unpatched `libvpx.a` is reused.

**Fix.** Put the patch bytes into `H1` (and so into `KEY`). Apply it with `patch -p1`: `git` is not in the
Android Docker image, while `patch` comes with `build-essential`.

### M4. No on-device exactness self-test for the Android NEON build

**Evidence.** The parent required `android/cs/aot/AndroidCodecs.cs` to be covered. It already checks VP8 NEON
output against the C reference on the device. Phase 2B drops it. The Android arm64 NEON build of the patched
path is checked only by a manual `shadow` run.

**Fix.** Add a VP9 case: a few-frame S2V9 fixture decoded through `s2a_vp9_nvdec`, with FNV hashes of the planes,
MVs and counts compared against the Mac C output.

### M5. The fuzz pass has no build recipe and no pass/fail rule

**Evidence.** The Risks table says "on Mac with ASan through the parity harness". Step 7 lists it under device
validation. No ASan configuration of `services/vpx/build.sh`, and no way to load the ASan runtime into a dotnet
host on macOS, is described.

**Fix.** Use a C fuzz driver over S2V9 records linked directly against an ASan build of the patched libvpx; there
is no dotnet involved. The criterion is no ASan report and no crash over 10^4 mutations with a fixed seed.

### M6. Speed is checked only at the end, and step 7 has no decision rules

**Evidence:**
- Phase 1 benchmarked P4G's own `frame_parallel_decoding_mode` 1. The hooked path forces 0, so it also counts
  symbols, accumulates per-tile counts, converts MVs twice and adds P/Invoke. None of that is measured before
  step 7.
- Step 7 measures 2 and 3 threads but does not say which wins, or what happens below 0.97x.

**Fix:**
- Extend `vpxbench` to run the hook on the 1033 captures once step 1 lands. Record mean and p95 against the
  5.2 ms baseline.
- State the rules: use the fewest threads that pass; below 0.97x with 3 threads, the owner decides before
  merging. Add the AGENTS.md conditions (thermal state, charging) and a 「期望 → 真结果」 table.

### M7. The plan does not map onto the house rule of short PRs

**Evidence.** AGENTS.md: "一个 PR 只做一件事…几小时内合入". The plan is 8 commits across libvpx, the Android
recipe, compat routing, platform registration and tools.

**Fix.** Name the PR boundaries. For example:
1. patch + recipes + lists;
2. jobgen;
3. header writer + SlotMap + harness;
4. routing (native off by default);
5. registration + default on.

### M8. The parity harness's memory model does not fit the code

**Evidence.** `Vp9Decoder.Decode` takes a `ResourceManager` over a concrete `DeviceMemoryManager(IVirtualMemoryManager)`
and a `SurfaceCache`. "A dictionary keyed by offset" is not an implementable seam. The harness also needs access
to internal Nvdec types.

**Fix.** Specify an `IVirtualMemoryManager` test implementation backed by one flat buffer, and the
`InternalsVisibleTo` (or reflection) route the harness uses.

### M9. Guest data now reaches libvpx 1.15.0's VP9 decoder in-process on Android

**Evidence.** Until now Android linked libvpx for VP8 only. This change exposes its VP9 decoder to
guest-controlled data.

**Fix.** Before writing the patch, check whether a newer 1.15.x point release carries decoder security fixes. If
one does, pin it instead, since the patch rebases at the same cost.

### M10. The `managed` switch cannot be used in Android release builds

**Evidence.** `S2A_*` variables reach the process only in debuggable builds (`DebugEnvironment.java`, per the
parent). A native regression on Android can therefore only be rolled back by shipping a new build.

**Fix.** State this as accepted. Alternatively, add a non-environment switch (for example a per-title or app
setting), if the owner wants one.

## Low

- **L1.** The parent `design_draft.md` (and `concepts_brief.md` and the earlier `reviews/`) exist only on branch
  `docs/vp9-native-design`, so the links in both documents on `perf/vp9-phase1` are broken. The local
  `upstream/main` does not contain `perf/vp9-arm64`, so the "Base: PR #86 merged" precondition is not met by the
  refs on this machine. Check this against the live upstream.
- **L2.** "Alternatives considered", third bullet: "Equal on all 1032 Phase 1 jobs" misquotes Phase 1. Phase 1
  measured that the MV read offset equals the previous write offset, not that libvpx's MVs equal the guest's.
- **L3.** Smaller items to pin down:
  - `S2A_VP9_PROBS_SIZE` and `S2A_VP9_COUNTS_SIZE` are placeholders. Write the numbers, since C cannot `sizeof` a
    C# struct.
  - `SurfaceCache.Invalidate` semantics: clear the entry (owner and size) rather than setting offset 0, which
    `Get` treats as "do not read".
  - Whether the native path rents its output surface from `rm.Cache`, as VP8 does.
  - How often `【红】` fires on repeated native errors.
  - Whether the Phase 1 capture/perturb hook stays.
  - Signed handling of the `byte` delta-Q and loop-filter delta fields in `PictureInfo`.
- **L4.** The hook "calls the normal `decoder_decode` path". That path runs `vp9_parse_superframe_index`
  (`vp9_dx_iface.c:362`) on the tile bytes. For conformant frames this is harmless, but a call that skips
  superframe parsing removes the question entirely.
- **L5.** Fallback exactness relies on the guest surface round trip (`SurfaceWriter` → `SurfaceReader`) and on
  border extension after read-back. The synthetic stream is 352x288. Add one size that is not a multiple of 8,
  and a GOP short enough (for example `-g 16`) to give mid-stream key frames, so the fallback → resume path is
  exercised in the gate.
- **L6.** Goal 2 should say it holds for conformant streams. For malformed tile data both decoders produce
  arbitrary output, and C# may throw (for example `Array4` index exceptions) where libvpx does not.

## Zoom-out summary

- **Right approach:** yes. The patch is the smallest change that reaches libvpx's speed and keeps the guest
  interface. Writing an uncompressed header reuses libvpx's own buffer and reference bookkeeping. That is
  simpler than filling `VP9_COMMON` by hand.
- **Over-built:** the export mode inside the shipped patch (H6). Everything else, including `shadow` mode, earns
  its place for an in-process decoder of guest data.
- **Missing:**
  - an early speed check of the hooked path (M6);
  - on-device NEON exactness (M4);
  - GRID and Apple scoping (H7);
  - test wiring (H5).
- **What will hurt later:**
  - stale MV bytes (C1), which would make every parity and `shadow` run noisy;
  - the routing exception (C2);
  - a patch-hash key placed in the wrong cache level (M3), which leaves an unpatched library in the local cache.
