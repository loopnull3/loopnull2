<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<p align="center">
  <img src="dist/hokkaido/hokkaido_logo.png" alt="Hokkaido" width="480">
</p>

# Hokkaido - Switch 2 Emulator for Android

Hokkaido is a Nintendo Switch 2 emulator for Android, maintained by [pavelgajdo](https://github.com/pavelgajdo). It continues the work of yuzu, Sudachi, Eden, [Eglantine](https://github.com/Djihads80/Eglantine) and [Lichii](https://github.com/zortos293/Lichii), and focuses on getting Switch 2 games running on Android phones, starting with Samsung's Exynos phones and their AMD-based Xclipse GPUs.

Hokkaido was called Lichii for Android until version 0.5.0. Installed copies update in place and keep their games, saves and settings.

Since 0.7.0 Hokkaido has two engines. Next to its own emulator it carries [Banana Native](https://git.eden-emu.dev/moceure/BananaNative), which runs a game's code natively on the phone instead of emulating it. You pick the engine per game, and both share Hokkaido's screens, GPU driver, saves and settings.

This is an independent, experimental project. It is not affiliated with Nintendo or with the projects it builds on.

## Download

- **Releases:** [github.com/pavelgajdo/Hokkaido/releases](https://github.com/pavelgajdo/Hokkaido/releases). Download the `.apk` from the latest `android-*` release and install it on your phone. You may need to allow installs from your browser or file manager.
- **Test builds:** the `android-latest` pre-release is rebuilt automatically from every change on `nx2`. It has the newest fixes and the newest bugs.

Requirements: an arm64 phone with Android 13 or newer and Vulkan 1.3. Games, keys and firmware are not included. Only use games and system files you have legally obtained.

## Two engines

| | Hokkaido | Banana Native |
|---|---|---|
| How it runs a game | Emulates the Switch 2's CPU (NCE) and GPU | Rewrites the game's code once ("Prepare") and runs it natively; draws through Vulkan |
| Games | Any game, with varying results | Only the games it has a profile for |
| First start | Starts at once | Prepares a pack from your dump first, which takes a few minutes |
| GPU on Xclipse phones | Samsung's driver or a custom RADV build | A custom RADV build (Samsung's driver has no placed memory maps) |

- **Choosing:** Game properties › **Engine**: Auto, Hokkaido or Banana Native. Auto picks Banana Native for the games its profiles call playable (Donkey Kong Bananza, Pokémon Pokopia, Mario Kart World, Fire Emblem: Fortune's Weave, Star Fox) and Hokkaido for the rest. The games Banana Native is still bringing up are offered as **Banana Native (experimental)**.
- **Shared:** the GPU driver (the game's own, else the global one), the save folder, and the resolution scale (1x is 1920x1080).
- **In game:** Back or the controller's menu button opens Hokkaido's menu on both engines. On Banana Native, Quick settings shows all of its in-game settings, live.
- **Game options:** Game properties › **Banana Native options** has full vegetation, the RAM saver (and its Strong level) and soft textures; **Build all shaders now** translates a game's whole shader set at once.
- **One APK:** Banana Native is built in. Its own launcher never shows, and nothing else needs installing.
- **Coming from the Banana Native app:** export the game's pack there (Manage › Export pack…), then in Hokkaido use Game properties › **Import Banana Native pack**, so the game doesn't need preparing again.

## Tested on

| | | | |
|---|---|---|---|
| Phone | Samsung Galaxy S26 (SM-S942B) | Samsung Galaxy S24 (SM-S921B) | Samsung Galaxy S25 (SM-S931B) |
| SoC | Exynos 2600 | Exynos 2400 | Snapdragon 8 Elite |
| GPU | Samsung Xclipse 960 (AMD RDNA3, GFX11) | Samsung Xclipse 940 (AMD RDNA3, GFX11) | Qualcomm Adreno 830 |
| RAM | 12 GB | 8 GB | 12 GB |
| OS | Android 17 (One UI) | Android 17 (One UI) | Android 17 (One UI) |
| GPU driver | Custom Mesa RADV build for Xclipse ("RADV Xclipse", Mesa 27.0-devel), installed through the app's GPU driver manager | Same | "8gen V39 Patched" Adreno driver, installed the same way |

Samsung's own driver on the S26 is also RADV-based and works, but it is slower on this phone than the custom RADV build.

The performance work tested on the S26's Exynos carries over well to Snapdragon: the S25 (Snapdragon 8 Elite) benefits from the same updates.

Other phones may work, but these are the only devices Hokkaido is tested on so far.

- **Mali GPUs (user reports):** on the Xiaomi 18 Fold (Mali-G2-Ultra), the stock driver shows a black picture in Fire Emblem: Fortune's Weave; players report that it draws with [faux123's PanVK driver](https://github.com/faux123/panvk-faux123-driver-rp4pro), installed through the GPU driver manager (issue #22).
- **Vulkan 1.3 is required.** A phone whose stock driver reports an older Vulkan version (for example 1.1 on some Snapdragon 8 Gen 1 tablets) needs a newer driver, such as a Turnip build for Adreno.

> 📺 **Game Testing & Video Footage:** Check out gameplay tests and video showcases on the [Exynos Gaming YouTube Channel](https://www.youtube.com/@ExynosGaming-t7k).

## Game status

| Game | Engine | Status |
|---|---|---|
| The Legend of Zelda: Ocarina of Time (Switch 2) | Banana Native (experimental) | Playable on the S26 with the custom RADV driver. Hokkaido sets it up for that phone: mesh draws through compute, Hold 60 fps, and lighter textures and memory pools on 12 GB phones. Some distant textures can show pink. |
| The Legend of Zelda: Ocarina of Time (Switch 2) | Hokkaido | Playable. Use 0.5x resolution; 16 GB of RAM recommended. It is no longer optimized for 12 GB phones, where it can run out of memory. |
| Pokémon Pokopia | Banana Native | Playable at 1x resolution, with the save shared with the Hokkaido engine. |
| Pokémon Pokopia | Hokkaido | Playable, also on 8 GB phones, with its sky (since 0.6.6), and with even frames since 0.7.1. It runs slower on the S26 than on the S24. |
| Donkey Kong Bananza | Banana Native | Prepares and starts. |
| Fire Emblem: Fortune's Weave | Hokkaido | Playable. Use 0.5x resolution and leave "Present a frame ahead" on Auto. |
| Orbitals (Global Friends Pass) | Hokkaido | Boots into gameplay. |
| Fast Fusion | Hokkaido | Goes into races, but at low frame rates (the game runs in slow motion). |

Only results tested on the phones above are listed here.

### What's different from upstream

- The Banana Native engine, built in and run behind Hokkaido's own screens and in-game menu.
- AV1 movies are decoded with Android's hardware decoder (MediaCodec).
- Ocarina of Time's movies skip the game's movie-enhancement neural network (which needs NVIDIA tensor cores), and show the movie frame directly.
- Emulated tensor-core math (HMMA) is faster on RADV.
- Memory limits tuned for phones, so games are closed for low memory less often.
- `EDEN_*` diagnostic switches can be set on Android with an `env.txt` file (see below).

## Recommended settings

- **System:** Handheld mode.
- **CPU:** NCE backend.
- **Graphics:** 0.5x resolution on the Hokkaido engine. Install a custom RADV driver if your phone has an Xclipse GPU; Banana Native needs one there.
- **Banana Native:** run a game's first Prepare on the charger. Expect missing objects and pop-in in the first sessions while its shaders build; each one is built only once.
- Close other apps before starting a game: Android closes games when memory runs low.

## Reporting problems

[Open an issue](https://github.com/pavelgajdo/Hokkaido/issues) with:

- your phone model, Android version and GPU driver;
- the game and what happened (screenshots help);
- the engine the game ran on;
- the log from the affected session: `Android/data/dev.lichii.android/files/log/eden_log.txt` for the Hokkaido engine, or the game's newest log in `Android/data/dev.lichii.android/files/logs/` for Banana Native (`dev.lichii.android.relWithDebInfo` for test builds).

Check logs for personal information before uploading. Never upload games, keys, firmware or account data.

### Diagnostic switches

Android can't pass environment variables to an app, so Hokkaido reads them from `Android/data/<app id>/files/env.txt`, one `KEY=VALUE` per line (`#` starts a comment). They take effect the next time the app starts. The available switches are listed in [AGENTS.md](AGENTS.md#debugging-tools). Leave the file empty for normal play: most switches slow the emulator down.

Banana Native reads its own `DM_*` switches from `env.txt` too, or from `env-<title ID>.txt` for one game, at the game's next start. Its switches are listed in Banana Native's `docs/env_switches.tsv`.

## Building

```bash
cd src/android
./gradlew assembleMainlineRelWithDebInfo
```

You need the Android SDK, NDK 28.2 and CMake 3.22. The APK is written to `src/android/app/build/outputs/apk/mainline/relWithDebInfo/`. CI builds every push (see [.github/workflows/android.yml](.github/workflows/android.yml)). Pushing a tag named `android-<version>` publishes a release with the APK.

Banana Native's Android sources are in `src/android/banana/upstream/`, with Hokkaido's changes as patches in `src/android/banana/patches/`. Its native libraries and assets come from the Banana Native release pinned in `src/android/banana/UPSTREAM.txt` (checked by SHA-256): the build downloads it, or takes a local copy with `-PbananaApk=<file>`.

- `tools/android/sync_bananative.sh <checkout> <tag> <apk> <url>` updates the embedded Banana Native to a release.
- `tools/android/build_bananative_native.sh <checkout> <folder> [title IDs]` builds Banana Native's game hosts from source; `-PbananaNativeLibs=<folder>` then puts them into the APK in place of the release's.

More documentation, inherited from upstream: [build guide](docs/Build.md), [coding guidelines](docs/Coding.md).

## Credits and license

Hokkaido continues the work of [Lichii](https://github.com/zortos293/Lichii) by Zortos and of Eglantine, Eden, yuzu and Sudachi, and their contributors, including Eglantine's original developer Camille LaVey. Lichii artwork by YOB (yobslab).

[Banana Native](https://git.eden-emu.dev/moceure/BananaNative) is by moceure and its contributors (MIT licence), based on [nxnative](https://github.com/seedds/nxnative) by seedds. Its frame generation uses Lossless Scaling's LSFG through Eden's port (GPL-3.0). Original copyright notices and third-party licenses are retained.

See [LICENSE.txt](LICENSE.txt) and the source files for the applicable license terms.
