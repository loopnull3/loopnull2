# LoopNative

LoopNative is a Switch 2 engine for the **Samsung Galaxy Z Flip5** (SM-F731\*, Snapdragon 8 Gen 2
for Galaxy, Adreno 740, 8 GB RAM) and the **Galaxy S26 / S26+ with Exynos 2600** (SM-S942\*,
SM-S947\*, Xclipse 960, 12 GB), built on [Hokkaido](https://github.com/pavelgajdo/Hokkaido)
android-0.7.1. It replaces Hokkaido's second engine, Banana Native, which this fork removes
entirely: no prebuilt Banana Native libraries are downloaded or shipped.

This document says what LoopNative does today, what it does not do yet, and the plan from here.

## What LoopNative is in 0.1

LoopNative 0.1 runs games on the emulator core Hokkaido already has (native CPU execution
through NCE, Ampere GPU emulation on Vulkan). What it adds is everything around the core that
decides whether a 12 GB console game survives on an 8 GB flip phone with a small hinge to shed
heat through:

| Part | Where | What it does |
|---|---|---|
| Device tuning | `src/loopnative/device.cpp` | Recognises the Flip5 (SM-F731\*, SCG23, SC-54D, codename `b5q`), the Fold5, other SM8550 phones and the S26 / S26+ with Exynos 2600 (by model and SoC, so the Snapdragon S26 is not mistaken for it); RAM tier, usable RAM, resolution default, the GPU maker's upscaler (AMD FSR on Xclipse, Qualcomm SGSR on Adreno), Turnip or RADV recommendation, thermal thresholds. |
| Title profiles | `src/loopnative/title_profiles.cpp` | Per-game settings and memory estimates (Ocarina of Time, Mario Kart World, Pokopia, Fire Emblem, DK Bananza, Star Fox), with a 0.75x render on 12 GB phones where upstream reports allow it, and whether the game fits this phone's RAM. |
| Smooth motion | `LoopNativeSession.kt` | Optional FSR 3 frame generation (Hokkaido's), per game, off by default. |
| Storage-backed guest memory | `src/common/host_memory.cpp`, `src/core/core.cpp` | The guest's RAM in a sparse, unlinked file on internal storage instead of a memfd, so Android writes cold pages to flash instead of closing the game. Per game, on by default when the game's estimate exceeds the phone's RAM. |
| Thermal governor | `src/loopnative/governor.cpp`, `LoopNativeGovernor.kt` | Reads `PowerManager.getThermalHeadroom` and the thermal status every 2 s; caps at 30 fps before the phone throttles, skips every other frame when very hot, lifts the cap after 60 s of cool. |
| Memory governor | same | `/proc/meminfo`, PSI where readable, `ActivityManager.lowMemory`: warns before lmkd closes the game. |
| Prepare | `src/loopnative/nso_analyzer.cpp`, `loopnative_jni.cpp` | Reads the game's ExeFS once (updates applied): every NSO module (LZ4 and Switch 2's ZBIC), its build ID, MOD0, symbols, SDK version, the NVN entry point (`nvnBootstrapLoader`) and every instruction a native runner has to intercept (SVC, TPIDRRO_EL0, CNTPCT_EL0, ...). Writes a pack manifest and shows a readiness report. |

In the app: **Game properties › Engine** picks Auto, Hokkaido or LoopNative; **LoopNative
options** has guest memory on storage, the thermal governor and "keep my settings";
**LoopNative report** runs Prepare again. Auto picks LoopNative for games with a profile and for
every game on an 8 GB phone.

### Honest status

- **Nothing here has run on a Flip5 or an S26 yet.** The code compiles and the portable core has host tests
  (`src/loopnative/tests`), but no game was booted with it on the phone it targets.
- **Memory is the wall.** Upstream reports Ocarina of Time running out of memory on 12 GB phones
  and Mario Kart World needing a 16 GB phone. The Flip5 has 8 GB. Storage-backed guest memory is
  the lever LoopNative pulls, and it is untested: expect stutter when pages come back from flash,
  and closures in the heaviest scenes. Pokémon Pokopia is the one profiled game reported running
  on 8 GB phones upstream.
- **Mario Kart World and Adreno 740:** its compute shaders use 48 KiB of shared memory; Adreno
  740 reports 32 KiB. What that costs in the picture is not known.
- **The GPU driver matters.** Qualcomm's stock driver lacks BC textures, so textures are decoded
  into more memory. Mesa Turnip (installable through the GPU driver manager) is recommended.

## Roadmap: a native engine

The goal behind the name is an engine that, like Banana Native did, runs a game's code natively
and its NVN graphics calls directly on Vulkan instead of emulating the GPU, but generic rather than
per-game, and written for Adreno 740. Prepare is the first piece of it. The steps:

1. **AOT patch table.** Keep the patch sites Prepare finds in the pack, so a start no longer scans
   the code (NCE patches at load time today). Measure the start-up time it saves on the Flip5.
2. **NVN interception.** Every Switch 2 game reaches NVN through `nvnBootstrapLoader`, which the
   sdk module exports; Prepare already finds it. Hook it, return a proc-address table that first
   passes every call through and counts it, to learn which NVN calls Ocarina of Time and Mario Kart
   World make and how often.
3. **NVN on Vulkan.** Implement that subset directly on Vulkan for Turnip on Adreno 740 (UBWC,
   LRZ, its 32 KiB compute shared memory), bypassing the Ampere command processor and its texture
   and buffer caches: both the CPU time and the duplicate memory they cost.
4. **Shader packs.** Translate a game's shaders during Prepare, on the charger, so play never waits
   on a pipeline.

Steps 2 to 4 need a Flip5 and legally dumped games to develop against: they are built one log
and one measurement at a time.

## Building

The APK: see [README.md](README.md#building). The portable core and its tests, on any Linux or
macOS machine:

```bash
cmake -S src/loopnative -B build/loopnative
cmake --build build/loopnative
ctest --test-dir build/loopnative --output-on-failure
```

CI runs those tests with the address and undefined-behaviour sanitizers
(`.github/workflows/loopnative-core.yml`) and builds the APK on every push
(`.github/workflows/android.yml`).

## Diagnostics

- `eden_log.txt` lines starting `LoopNative:` show the phone it recognised, each setting it
  applied, where guest memory lives and every governor step.
- The pack manifest of a prepared game is at
  `Android/data/dev.loopnull.loopnative/files/loopnative/packs/<title ID>/manifest.json`.
