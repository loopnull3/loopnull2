<!--
# SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

# LoopNative - Switch 2 on the Galaxy Z Flip5 and Galaxy S26

LoopNative is a Nintendo Switch 2 emulator for Android tuned for the **Samsung Galaxy Z Flip5**
(Snapdragon 8 Gen 2 for Galaxy, Adreno 740, 8 GB RAM) and the **Samsung Galaxy S26 / S26+ with
Exynos 2600** (Xclipse 960, 12 GB). It is a fork of
[Hokkaido](https://github.com/pavelgajdo/Hokkaido) android-0.7.1, which continues yuzu, Sudachi,
Eden, Eglantine and Lichii.

Hokkaido carries two engines: its emulator and Banana Native. LoopNative removes Banana Native
completely and puts its own engine, **LoopNative**, next to the emulator. See
[LOOPNATIVE.md](LOOPNATIVE.md) for what it does, its honest status and the roadmap to a native
(NVN-on-Vulkan) engine.

This is an independent, experimental project, not affiliated with Nintendo, Samsung or the
projects it builds on. Games, keys and firmware are not included: only use games and system files
you have legally obtained.

## Download

CI builds an arm64 APK on every push (Actions › android › the run's `loopnative-android-*`
artifact). The app installs next to Hokkaido (`dev.loopnull.loopnative`) and keeps its own games,
saves and settings.

Requirements: an arm64 phone with Android 13 or newer and Vulkan 1.3.

## Two engines

| | Hokkaido | LoopNative |
|---|---|---|
| How it runs a game | The emulator core (NCE CPU, emulated GPU) with your settings | The same core, started with the phone's and the game's tuning |
| Memory | Guest memory in RAM | Guest memory on storage when the game needs more RAM than the phone has |
| Heat | Android's own throttling | A governor caps at 30 fps before the phone throttles, lifts it once cool |
| First start | At once | Prepare reads the game's code once (seconds) and shows a readiness report |

- **Choosing:** Game properties › **Engine**: Auto, Hokkaido or LoopNative. Auto picks LoopNative
  for the games it has a profile for (Ocarina of Time, Mario Kart World, Pokémon Pokopia, Fire
  Emblem: Fortune's Weave, Donkey Kong Bananza, Star Fox) and for every game on an 8 GB phone.
- **Options:** Game properties › **LoopNative options**: guest memory on storage, thermal
  governor, keep my settings, smooth motion (FSR 3 frame generation, off by default).
  **LoopNative report** runs Prepare again.

## Galaxy Z Flip5

Nothing has been tested on a Flip5 yet. What the upstream reports say about the games you would
want on it:

| Game | Upstream (12-16 GB phones) | On an 8 GB Flip5 |
|---|---|---|
| The Legend of Zelda: Ocarina of Time | Playable at 0.5x; 16 GB recommended, 12 GB can run out of memory | Needs guest memory on storage; expect closures in heavy scenes |
| Mario Kart World | Banana Native's profile: needs a 16 GB phone | Needs guest memory on storage; LoopNative also uses 0.5x and the game's 30 fps mode |
| Pokémon Pokopia | Playable, also on 8 GB phones | Best candidate |
| Fire Emblem: Fortune's Weave | Playable at 0.5x | Tight |

Recommended on the Flip5:

- **GPU driver:** Mesa Turnip through the GPU driver manager (the stock driver has no BC
  textures, which costs memory).
- **Storage:** about 10 GB free for guest memory on storage.
- **Flex mode:** half-fold the phone and the game takes the top half, the controls the bottom
  half (Hokkaido's foldable layout).
- Close other apps first, and play on a cool phone: the Flip5 throttles within minutes under full
  GPU load.

## Galaxy S26 (Exynos 2600)

The S26 with Exynos 2600 is Hokkaido's own test phone, so here the upstream reports are about this
very phone. LoopNative's S26 profile:

- **Picture:** a 0.5x or 0.75x render (per game, below) upscaled to the screen with AMD FSR,
  which suits the S26's AMD-based Xclipse GPU: sharp at a fraction of a 1x render's cost.
- **GPU driver:** a custom Mesa RADV build is recommended; upstream measured it faster than
  Samsung's own driver on the S26.
- **Memory:** 12 GB. Ocarina of Time can still run out of memory there upstream, so its guest
  memory goes on storage by default.
- **Heat:** the S26 holds load far longer than a flip phone, so the governor caps later (forecast
  0.90) and lifts the cap after 30 s of cool.
- **Smooth motion (optional):** FSR 3 frame generation shows a generated frame between every two
  drawn ones, so a 30 fps game moves like 60. Off by default: it adds a little input delay.

| Game | Upstream on the S26 | LoopNative on the S26 |
|---|---|---|
| The Legend of Zelda: Ocarina of Time | Hokkaido engine: playable at 0.5x; 12 GB can run out of memory | 0.5x + FSR, guest memory on storage |
| Mario Kart World | Hokkaido's own preset; Banana Native's profile asked for 16 GB | 0.75x + FSR, the game's 30 fps mode, guest memory on storage |
| Pokémon Pokopia | Playable (slower on the S26 than on the S24) | 0.75x + FSR |
| Fire Emblem: Fortune's Weave | Playable at 0.5x | 0.5x + FSR |

"Without lag" is the goal, not a promise: shader builds the first time a scene appears, scene
loads and, for the largest games, memory coming back from storage can still stutter.

## Reporting problems

Include your phone model, Android version and GPU driver, the game, the engine, the LoopNative
report, and `Android/data/dev.loopnull.loopnative/files/log/eden_log.txt` (its `LoopNative:`
lines show what LoopNative did). Check logs for personal information before sharing. Never share
games, keys, firmware or account data.

### Diagnostic switches

Android can't pass environment variables to an app, so the app reads them from
`Android/data/<app id>/files/env.txt`, one `KEY=VALUE` per line (`#` starts a comment). They take
effect the next time the app starts. The available switches are listed in
[AGENTS.md](AGENTS.md#debugging-tools). Leave the file empty for normal play.

## Building

```bash
cd src/android
./gradlew assembleMainlineRelease
```

You need the Android SDK, NDK 28.2 and CMake 3.31. The APK is written to
`src/android/app/build/outputs/apk/mainline/release/`. LoopNative's portable core builds and tests
on its own: see [LOOPNATIVE.md](LOOPNATIVE.md#building).

More documentation, inherited from upstream: [build guide](docs/Build.md),
[coding guidelines](docs/Coding.md).

## Credits and license

LoopNative builds on [Hokkaido](https://github.com/pavelgajdo/Hokkaido) by pavelgajdo, which
continues [Lichii](https://github.com/zortos293/Lichii) by Zortos and Eglantine, Eden, yuzu and
Sudachi, and their contributors, including Eglantine's original developer Camille LaVey. Original
copyright notices and third-party licenses are retained.

See [LICENSE.txt](LICENSE.txt) and the source files for the applicable license terms.
