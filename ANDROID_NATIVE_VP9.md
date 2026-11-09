# Full-speed VP9 cutscenes on Android

This branch is upstream S2Arm `main` (`2be4b3be`, 2026-11-09) plus three changes
that are not merged upstream yet. With them, Persona 4 Golden's 2D animated movies
play at full speed and in sync with the audio on Android. Before, they played at
about 0.7× speed and fell out of sync.

| Commit | Upstream PR | What it does |
|---|---|---|
| 1 | [#226](https://github.com/jhaizhou-ops/S2Arm/pull/226) | Fixes the C# VP9 decoder: its deblocking filter was effectively off (`vp9_loop_filter_init` was never ported). All platforms. |
| 2 | [#231](https://github.com/jhaizhou-ops/S2Arm/pull/231) | Patches libvpx 1.17.0 so it can decode the VP9 jobs a game sends to the Switch's video decoder (NVDEC). Built on all platforms, called on none yet. |
| 3 | [#247](https://github.com/jhaizhou-ops/S2Arm/pull/247) | Sends those jobs to the patched libvpx instead of the C# decoder. **On by default on Android only**; Apple platforms are unchanged. |

Any job libvpx cannot decode exactly the way the C# decoder would goes to the C#
decoder, and libvpx takes over again at the next key frame.

## Results

Measured on a ROG Phone 6D (Dimensity 9000+, Android 14), Persona 4 Golden
(010062B01525C000), opening movie, three cold starts:

| Build | Playback speed |
|---|---|
| upstream `main` (C# decoder) | 0.70× |
| with these three changes | 1.01×, 1.00×, 1.00× |

- 30 new frames per second, 33 ms apart (37 ms at the 90th percentile).
- The video decoder uses about a third of one CPU core.
- On 1033 frames captured from the device, libvpx gives byte-identical pictures,
  motion vectors and decoder counts to the fixed C# decoder.

Only Persona 4 Golden has been tried. Other games that play VP9 movies through
NVDEC should benefit too, but nobody has checked them yet.

## How to use it

You need to be able to build and install your own game for Android with S2Arm
already. Android is still in bring-up upstream, so start from
[`pack/targets/android/README.md`](pack/targets/android/README.md). As always,
bring your own game files and keys; nothing from a game is in this repository.

1. Get this branch:

   ```sh
   git clone -b android/native-vp9 https://github.com/Catnip202X/S2Arm.git
   ```

2. Build and install your game exactly as you would from upstream, for example:

   ```sh
   bash pack/oneclick/oneclick.sh <game dir> --target android --keys <prod.keys> --out <private dir>
   ```

   No extra settings are needed. The build fetches libvpx 1.17.0 (pinned by
   sha256) and applies the patch from `services/vpx/patches/`.

3. Play a movie and check the log:

   ```sh
   adb logcat | grep 'VP9'
   ```

   `VP9（libvpx 原生，2 线程）首帧解出：1920x1080` means libvpx is decoding. A line
   `VP9 原生解码 → 托管解码器` means a job went to the C# decoder; an occasional
   one is expected.

To compare against the C# decoder in a debuggable build, start the game with
`--es s2arm.debug_env 'S2A_NVDEC_VP9=managed'` (see "Debug-only cold-process
environment" in [`android/README.md`](android/README.md)). Release builds have
no switch.

## When upstream catches up

Once the three PRs above are merged, use upstream `main` instead; this branch
will not be needed.
