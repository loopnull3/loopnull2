# LoopNative

LoopNative is a Switch 2 engine for the **Samsung Galaxy Z Flip5** (SM-F731\*, Snapdragon 8 Gen 2
for Galaxy, Adreno 740, 8 GB RAM) and the **Galaxy S26 / S26+ with Exynos 2600** (SM-S942\*,
SM-S947\*, Xclipse 960, 12 GB), built on [Hokkaido](https://github.com/pavelgajdo/Hokkaido)
android-0.7.1. Next to it sits **LoopNative Pack**: Hokkaido's second engine, Banana Native, with
LoopNative's changes for these phones (below).

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
| Build all shaders now | `ShaderPrecache.kt`, `native_shader_precache.cpp` | The core's shader precache (`video_core/shader_warm`), which upstream offers on the desktop only: the game's compute shaders are found in its files once, then built with the saved graphics pipelines on the loading screen of each start. For the Hokkaido and LoopNative engines. |
| Storage-backed guest memory | `src/common/host_memory.cpp`, `src/core/core.cpp` | The guest's RAM in a sparse, unlinked file on internal storage instead of a memfd, so Android writes cold pages to flash instead of closing the game. Per game, on by default when the game's estimate exceeds the phone's RAM. |
| Thermal governor | `src/loopnative/governor.cpp`, `LoopNativeGovernor.kt` | Reads `PowerManager.getThermalHeadroom` and the thermal status every 2 s; caps at 30 fps before the phone throttles, skips every other frame when very hot, lifts the cap after 60 s of cool. |
| Memory governor | same | `/proc/meminfo`, PSI where readable, `ActivityManager.lowMemory`: warns before lmkd closes the game. |
| Prepare | `src/loopnative/nso_analyzer.cpp`, `loopnative_jni.cpp` | Reads the game's ExeFS once (updates applied): every NSO module (LZ4 and Switch 2's ZBIC), its build ID, MOD0, symbols, SDK version, the NVN entry point (`nvnBootstrapLoader`) and every instruction a native runner has to intercept (SVC, TPIDRRO_EL0, CNTPCT_EL0, ...). Writes a pack manifest and shows a readiness report. |

In the app: **Game properties › Engine** picks Auto, Hokkaido or LoopNative; **LoopNative
options** has guest memory on storage, the thermal governor and "keep my settings";
**LoopNative report** runs Prepare again. Auto picks LoopNative for games with a profile and for
every game on an 8 GB phone.

### Honest status

- **Nothing here has run on a Flip5 or an S26 yet**, LoopNative Pack's 8 GB class included: no
  one has reported Banana Native on an 8 GB phone. The code compiles and the portable core has host tests
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

## LoopNative Pack: Banana Native, modified

LoopNative 0.1 reads a game's files as they are. The engine that rebuilds them is
[Banana Native](https://git.eden-emu.dev/moceure/BananaNative) (MIT): Prepare rewrites the game's
code and translates its shaders into a pack, then the code runs natively and NVN draws through
Vulkan. Writing that from nothing is years of work, so LoopNative Pack takes Banana Native v0.3.45
as Hokkaido embeds it (`src/android/banana`: its Android sources in `upstream/`, Hokkaido's
patches, its native libraries from the pinned release) and changes it for these phones.

| Change | Where | Why |
|---|---|---|
| 8 GB class | `LoopNativeTuning.java`, from `HokkaidoBridge.prepareStart` | Banana Native's lowest class is "12 GB or less". On 8 GB a game starts with RAM saver: Strong, less vegetation, pools placed in 256 MiB parts and a render of 1280x720 at most. Set once per game; the player's later changes stay. |
| Memory class | `LoopNativeTuning.writeMemoryClass` | On phones of 12 GB or less every game gets `DM_VK_PLACE_PART_MB=256` (pools placed by parts), which Banana Native's profiles give one game only. The texture cache keeps its measured limit. A smaller reported total (`DM_APP_POOL_MB`) was tried and removed: Mario Kart World takes a 7.04 GiB block from a heap sized by that total and crashed in `nnMain` on a Galaxy S26 with 5632 MiB reported. |
| Auto by phone | `GameEngine.autoIsPack` | The pack only where it can play: the profile's required RAM against the phone's, a RADV driver on Xclipse, and Ocarina of Time on the S26 (bring-up in its profile, playable there upstream). |
| RAM requirement read from the profile | `LoopNativeTuning.requiredRamGib` | Banana Native measured them on its own engine (Mario Kart World 14 GiB, Bananza 10). |

| NaN-exact min / max / clamp | `pack_tools/nan_exact.py`, appended to the release's shader tools at build time | RADV on Xclipse did not keep GLSL's NaN rules: white picture and white blocks in Mario Kart World and Ocarina of Time. Found and fixed on a Galaxy S26. |
| ASTC blocks that are no ASTC data | `native_patches/0001-astc-error-colour-blocks.patch` (with `native_patches/astcenc`) | Pink error-colour texels become transparent zero and are logged; a level read mid-rewrite keeps its last whole content; unmapped levels come from the level above; HDR endpoint modes become LDR. |
| D24 depth bias | `native_patches/0002-d24-depth-bias.patch` | A constant depth bias kept where a D24 target is made D32F. |
| Old shaders dropped | `HokkaidoBridge.dropOldShaders` | Shaders built with other shader tools are removed once, so the fixes above reach games already prepared. |

The last four come from [loopnull3/Hokkaido](https://github.com/loopnull3/Hokkaido), branch
`ccr-2855a939-9ewczd` (up to `acc997c`), where they were made against a Galaxy S26. Its three
shader passes that read exact pink as 0 are left out, as that branch's last commit does: they
turned valid data black. CI builds the game hosts from Banana Native v0.3.45's source with the
patches (`tools/android/build_bananative_hosts.sh`); the packer's libraries stay the release's.

Further native changes (a profile's `low_ram_*` keys for an 8 GB class, an Adreno 740 path) go the
same way, as patches in `native_patches/`, and each needs a log from the phone to justify it. Known limits
on these phones, from Banana Native's own notes: it is tested on Snapdragon 8 Elite phones with
16 GB; Mario Kart World's compute shaders want 48 KiB of shared memory (Adreno 740 has 32);
Fire Emblem needs Turnip on Adreno 740.

## Roadmap: LoopNative's own native path

LoopNative's own engine would, like Banana Native, run a game's code natively and its NVN
graphics calls directly on Vulkan, but generic rather than per-game. Prepare is the first piece
of it. The steps:

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
- **LoopNative Pack options › Diagnostic switches** takes Banana Native switches for one game
  (`KEY=VALUE`, one a line; Banana Native's Extra environment). The host reads them last, so they
  win over the game's other settings, and the run's log lists each as an `[adv]` line: a switch
  can be A/B'd on the phone without another build (`DM_VK_MESH_EMU=0`, `DM_VK_MESH_SKIP=<mesh
  shader key>`, `DM_VK_DRAW_SKIP_KEY=<pixel shader key>`, `DM_VK_MESH_LOG=1`). Game properties
  shows how many are set.
