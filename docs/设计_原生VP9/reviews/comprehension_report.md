# Comprehension Goldfish: design_draft.md (full file, rev 1)

Reviewer: fresh subagent, persona `agents/comprehension.md` (elephant-goldfish-model), no conversation context.

## Comprehension test

| # | Question | Result | Where / why |
|---|---|---|---|
| 1 | What is broken, and why does decoder speed equal playback speed? | Answered | The Problem |
| 2 | Why can't the NVDEC job just be handed to libvpx? | Answered | Overview; Facts |
| 3 | What does each option write back to the guest (picture, MVs, counts)? | Contradictory | Data flow vs Option 0 vs Goal 2 vs Phase 3 replay |
| 4 | Which frames does each option send to the C# decoder? | Contradictory for B | Option B vs Routing |
| 5 | Why can't Option A detect a frame-parallel stream from the inputs? | Partly answered | compressed into one sentence |
| 6 | How does Option 0 find the frame start in the 4 KiB window, and handle superframes? | Not answered | Option 0 |
| 7 | What do `【红】`, superframe, hidden alt-ref, row-MT, block-linear, NV12/I420, VIC, syncpoint, "context 0" mean? | Not answered | no glossary |
| 8 | What evidence picks 0, A or B, and who decides? | Partly answered | no decision criteria |
| 9 | Is the speed target 1.0x or 0.97x? | Contradictory | Goal 1 vs Readings |
| 10 | Does Android libvpx already decode VP9? | Contradictory across docs | brief vs draft |

2 of 10 fully answered, 3 partly, 5 contradictory or missing.

## Findings

### Critical
- **C1.** Goal 2 and the replay test require MV and count bytes identical to the C# decoder, but Options 0 and A zero-fill them, so they can never pass as written. Fix: per-option output table (picture / MV / counts / compared in replay); Goal 2 = picture always, MV and counts for the buffers step 1 shows the guest reads.
- **C2.** Option 0's "small accessor" for MVs is never described and conflicts with "no libvpx patch" (stock libvpx has no MV API). Fix: zero-fill MVs in 0 (fallback exact only at key frames), or name the internals-reading shim and list it.

### High
- **H1.** Option B is inconsistent about intra-only frames ("every 8-bit stream" vs "fails when intra-only"; Routing marks intra-only as A-only). Pick one answer with the reason.
- **H2.** The argument that A cannot detect frame-parallel streams is too compressed; spell out the four steps (UV tables only move via adaptation from our counts; we return zero; so they never move; the test is blind).
- **H3.** Option 0 lacks a mechanism: how to find the frame start in the 4 KiB window, how superframes map onto NVDEC jobs, how a false header match is ruled out at runtime.
- **H4.** Undefined terms and unlinked house rules (`【红】`, superframe, alt-ref, frame context, `use_prev_frame_mvs`, row-MT, NV12/I420, block-linear, VIC, syncpoint, "house rule" no FFmpeg, "no per-game configuration"). Add a glossary; link AGENTS.md.

### Medium
- **M1.** Speed target 1.0x vs ≥ 0.97x vs "reaches 1.0x": make it one number and state the noise.
- **M2.** "Recording-to-source matching" undefined; add one sentence and a link.
- **M3.** Brief options 1/2 vs draft 0/A/B have no mapping.
- **M4.** Docs disagree on whether VP9 is in the libvpx builds (Apple yes, Android `--disable-vp9`).
- **M5.** No decision criteria from step-1 answers to options; add a decision table.
- **M6.** "33 ms with headroom": give a number and the pinning method.
- **M7.** Option 0's fallback reasoning: driver tables are only wrong for adapting streams.
- **M8.** Slot table edge cases: key frame reset, Last/Golden/Alt pointing at one surface, resize.

### Low
- L1 cite the 4096x2304 limit; L2 "2 threads until Phase 1 replaces it"; L3 how env vars reach an Android app; L4 link the `compat/patch_new.sh` workflow; L5 whether hidden-frame copy is needed for every option; L6 define the route's "stream state".

## Verdict

Not ready for implementation review. Phase 1 is clear and self-contained; Phase 2 contradicts itself on what each option writes back and what replay compares (C1), refers to an undefined MV accessor (C2), is inconsistent on B and intra-only (H1), has no mechanism for Option 0's header search or superframes (H3), and leans on undefined terms (H4). A per-option output table, a glossary and a step-1 decision table cover most of it.
