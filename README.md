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

Hokkaido carries two engines: its emulator and Banana Native. This fork has three: the emulator,
**LoopNative** (the emulator tuned for the phone) and **LoopNative Pack**, which is
[Banana Native](https://git.eden-emu.dev/moceure/BananaNative) with LoopNative's changes for these
phones. See [LOOPNATIVE.md](LOOPNATIVE.md) for what each does and its honest status.

This is an independent, experimental project, not affiliated with Nintendo, Samsung or the
projects it builds on. Games, keys and firmware are not included: only use games and system files
you have legally obtained.

## Download

CI builds an arm64 APK on every push (Actions › android › the run's `loopnative-android-*`
artifact). The app installs next to Hokkaido (`dev.loopnull.loopnative`) and keeps its own games,
saves and settings.

Requirements: an arm64 phone with Android 13 or newer and Vulkan 1.3.

## Three engines

| | Hokkaido | LoopNative | LoopNative Pack |
|---|---|---|---|
| How it runs a game | The emulator core (NCE CPU, emulated GPU) with your settings | The same core, started with the phone's and the game's tuning | Banana Native: the game's code rewritten once and run natively, its graphics (NVN) drawn directly with Vulkan |
| Games | Any | Any | Only the games Banana Native has a profile for |
| Game files | Read as they are | Read as they are | Rebuilt into a **pack** (code and shaders) by Prepare |
| First start | At once | Prepare reads the game's code (seconds) and shows a report | Prepare makes the pack: minutes for the code, longer for the shaders |
| Memory | Guest memory in RAM | Guest memory on storage when the game needs more than the phone has | The game's memory pools are the GPU's own (no second copy); RAM saver keeps them in a file |
| GPU driver on Xclipse | Samsung's or custom RADV | Samsung's or custom RADV | Custom RADV only (Samsung's has no placed memory maps) |

- **Choosing:** Game properties › **Engine**: Auto, Hokkaido, LoopNative or LoopNative Pack. Auto
  picks the pack for the games Banana Native's profiles call playable (Pokémon Pokopia, Fire
  Emblem: Fortune's Weave, Star Fox, Donkey Kong Bananza, Mario Kart World) when the phone has the
  RAM the profile requires (Mario Kart World: 14 GiB; Bananza: 10) and, on Xclipse, a RADV driver;
  for Ocarina of Time on the Galaxy S26 with RADV, where upstream reports it playable; otherwise
  LoopNative for profiled games and on 8 GB phones.
- **LoopNative options:** guest memory on storage, thermal governor, keep my settings, smooth
  motion (FSR 3 frame generation, off by default). **LoopNative report** runs Prepare again.
- **LoopNative Pack options:** full vegetation, RAM saver, RAM saver: Strong, soft textures;
  **Import a Banana Native pack**; **Build all shaders now**.
- **Build all shaders now (Hokkaido engine):** for the Hokkaido and LoopNative engines. Reads the
  game's files once and finds its compute shaders; each start then builds them, and the graphics
  pipelines saved from earlier play, on the loading screen instead of while you play, and again
  only when the GPU driver changes. It is the emulator's shader precache, which upstream has in
  its desktop app only. Graphics pipelines the game has never drawn can't be built ahead: the
  emulator learns them from the game's draws. **Clear shader cache** removes it.

### What LoopNative changes in Banana Native

Banana Native asks for a 12 GB phone and has a low-RAM class for "12 GB or less". LoopNative adds
a class below it for **8 GB phones** (the Galaxy Z Flip5), set once per game and changeable after:

- RAM saver: Strong from the first start (file-backed shader and texture pools, the texture cache
  at a quarter of RAM, textures without their two finest levels);
- less vegetation and a render of 1280x720 at most.

**Memory class** (LoopNative Pack options › Memory class, on by default on phones of 12 GB or
less): every game's memory pools are placed in 256 MB parts instead of whole, as Banana Native's
low-RAM profile key does for the one game that has it, and the texture cache is left to measure
its own limit from the phone's free memory.

An earlier build also told the game it had 5.5 GB (12 GB phones) or 4 GB (8 GB phones) instead of
9, so that it would keep smaller caches. That is gone: Mario Kart World sizes its heap from that
number at startup and then takes one 7.04 GiB block from it, so it crashed at once on a Galaxy
S26. The game needs the full report, which is why its profile asks for 14 GiB of RAM.

With its memory class on, Auto lets a 12 GB phone start a game on the pack whose profile asks for
more RAM (Mario Kart World: 14 GiB): it starts, and Android may close it when memory runs short.
On 8 GB phones Auto still starts such games on LoopNative; the pack can be chosen by hand.

On the **Galaxy S26** the pack engine keeps what Hokkaido tuned on that very phone (Ocarina of
Time: mesh draws through compute on RADV, Hold 60 fps, lighter textures and pools), and takes the
fixes made on an S26 with RADV in [loopnull3/Hokkaido](https://github.com/loopnull3/Hokkaido)
(branch `ccr-2855a939-9ewczd`, up to `acc997c`):

- **White picture and white blocks** (Mario Kart World, Ocarina of Time): RADV on Xclipse did not
  keep GLSL's NaN rules, so the fog shaders' NaN spread through bloom. The shader tools now write
  min / max / clamp with the Switch's NaN results spelled out (`pack_tools/nan_exact.py`).
- **Pink textures:** ASTC blocks that hold no ASTC data (empty or damaged storage) become
  transparent zero texels instead of ASTC's pink error colour, and their place is logged; a level
  read while the game rewrites it keeps its last whole content; a level in unmapped storage is made
  from the level above; HDR endpoint modes are re-encoded as LDR
  (`native_patches/0001-astc-error-colour-blocks.patch`).
- **Depth bias** kept where a D24 target is made D32F (`native_patches/0002-d24-depth-bias.patch`).
- **Mario Kart World stopping after a race**, before the result movie (upstream: about one race in
  five on 1.0.0): Banana Native's fix from its v0.3.52 preview, taken alone
  (`native_patches/0004-codec-registered-buffers.patch`). Not run on a phone here.
- **Soft textures** shows and keeps the game's default (Mario Kart World's profile has them on)
  instead of holding it off.
- **Mario Kart World, pink at a distance (a fix under test):** on an S26 the road surface is
  pink in bands from some distance on, right up close, and the border moves with the kart; on
  the title screen a far flag has magenta pieces while the same banner up close is right.
  Measured on the phone with `DM_VK_COPY_AUDIT=1` (`native_patches/0003-astc-copy-audit.patch`):
  - the driver draws all-zero blocks, HDR blocks and illegal void-extent blocks as (1, 0, 1, 1)
    and every legal LDR block tried right;
  - the game's uploads hold none but all-zero blocks, which are taken out, and the terrain
    arrays and every texture copied into them hold none when read back;
  - in the game's own textures the phone samples whole small levels wrong while a copy out of
    the same level returns the valid blocks written into it: 124 of 684 sampled levels in one
    run, in 117 of 218 textures (level 5 of a 1024x384 and of a 256x384 array, level 6 of
    746x746 textures); 95 of them wholly as that error colour, the others partly or as other
    content. All 124 are named by RADV's own test for writing a level through addrlib's
    non-block-compressed view, with the first level's block count aligned up to 16 taken as its
    padding: a level is one when for its width or height `max(min(max(blocks << level, first),
    padding) >> level, 1)` is less than its block count. The rule names 34 more levels that
    sample right: in larger textures (level 6 of 1024x768), whose padding may be wider, and a
    few where that view is right (level 5 of a 51x819 texture, whose level 6 is wrong). A
    texture of 3 x 2^n texels has one such level, 12 texels wide: a band at one distance.
    A second run, with the fix below on: 1068 of 2155 sampled levels, in 859 of 1152 textures,
    561 of them wholly the error colour; the rule names 1066 (and 68 that sample right). The
    other two are levels 5 and 6 of a 1434x1434 array of 46 layers;
  - the audit's own test textures (`mip check`) say which blocks. Of three of the game's sizes
    as ASTC 8x8, 256x384 has level 5 as the error colour, 51x819 levels 5 and 6, and 1024x768
    is right at every level. With 256x384's block counts, ASTC 6x6 (192x288) fails the same
    way and BC3 and ASTC 4x4 (128x192) are right at every level. What is written to such a
    level lands a level up: two of level 4's six blocks hold level 5's two. Mesa asks addrlib for
    that view of every 16-byte block as BC3's, which has 4x4 texels a block (a reading of
    Mesa's source): right for BC3 and ASTC 4x4, a level off for larger blocks;
  - the game copies texture to texture only, and fills every layer of a terrain array, at
    every level, from one texture (serial 130 or 132) before the tiles' own textures follow.

  The fix under test (`native_patches/0006-radv-astc-level-cap.patch`): with RADV a sampled
  view of an ASTC image of blocks larger than 4x4 ends before its first such level, so the level
  before it is drawn in its place (`DM_VK_ASTC_LEVEL_CAP=0` in Diagnostic switches turns it
  off). It ran on an S26: the log shows the views cut, and the pink is still there. That
  run's log has one texture the cut cannot explain: a 1024x1024 array of 46 layers whose levels
  of 64x64 texels or less are all-zero blocks in every layer, which is the error colour at a
  distance and nothing wrong up close. Whether the driver loses the writes into those levels
  or the uploads never carried them is not known; `native_patches/0007-astc-array-audit.patch`
  makes the next audit run say which. Still open: the level drawn in its place has its first
  blocks overwritten by the next level's upload, and the terrain arrays' first fill is
  (1, 0, 1), which may be data and no colour. Tried and taken
  out: soft textures, dense images for the sparse ASTC arrays, cleaning buffer copies (the game
  makes none), illegal void extents (the game has none), and a CPU copy for levels of sizes
  that are no power of two (never run).
- **Mario Kart World, light-blue pillars and spikes into the sky (a fix under test):** on an S26
  they stand where there is no water, and go when mesh draws are skipped (`DM_VK_MESH_EMU=0`);
  the game's mesh programs there are its water (a fluid simulation, river cascades) and its
  trails. The likely cause, not proven: the game's sparse render targets (8192x8192 RG16 and
  16384x16384 R8) get memory tile by tile, and a tile that got memory after the texture's first
  use started with what that memory last held (Banana Native lists it as open). Tile memory is
  shared by every sparse image, so that can be the bytes of an ASTC array's tile read as
  heights (a driver without sparse residency for those formats gets whole images, which have
  no such tiles). Such a tile now starts as zeros, which is what it read before it had memory
  (`native_patches/0005-sparse-gpu-fresh-tiles.patch`; `DM_VK_SPARSE_GPU_ZERO=0` in Diagnostic
  switches turns it off). It ran on the phone in the title scene only, where no pillar shows.
- Shaders built with older shader tools are dropped once, so the game builds them again.
- Not included, as on that branch: the shader passes that read exact pink as 0. They also turned
  valid data black (dark patches on desert ground, black spreading on the title screen).

CI builds the game hosts from Banana Native v0.3.51's source with those patches
(`tools/android/build_bananative_hosts.sh`); the packer's libraries stay the release's.

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

## Less stutter

Most stutter is a shader being built the first time a scene needs it. Both kinds of engine can
build ahead of play:

- **LoopNative Pack:** a prepared game without shaders asks once, before its first start,
  whether to build them all first (**Build first**, 20 to 45 minutes for a large game). Later:
  Game properties › **Build all shaders now**.
- **Hokkaido and LoopNative:** LoopNative's report has **Build shaders first**; Game properties
  has **Build all shaders now (Hokkaido engine)**. The compute shaders in the game's files and the
  graphics pipelines saved from earlier play are then built on the loading screen. Graphics
  pipelines never seen before are still built when they first appear.

What is left: the first time a never-seen graphics pipeline appears (emulator engines), scene
loads, memory coming back from storage on phones with less RAM than the game uses, and the
phone's own thermal throttling, which the governor's 30 fps cap is there to stay ahead of.

## Two Joy-Cons as one controller

A pair of Joy-Cons connected over Bluetooth is two controllers to Android, and **Auto-map a
controller** takes one. Settings › Controls › a player › **Auto-map two Joy-Cons** asks for the
controller in your left hand, then the one in your right, and gives the player the left half of
the controls from the first and the right half from the second. It works with any two
controllers, and each button can still be remapped by hand afterwards.

Controller ports now follow the controllers themselves rather than the order they connected in,
so the pair keeps its mapping whichever Joy-Con reconnects first. If you used several controllers
before, auto-map them once more.

## A second player

A controller that is in no player's mapping becomes the next free player when a button is
pressed on it during a game, with its own default mapping; a toast names the player. This is
not saved: Settings › Controls gives a player its controller for good. One Joy-Con of a pair is
not made a player on its own. A controller that a saved mapping names keeps the port the
mapping names, whichever other controllers are connected.

A game that adds a local player asks which user that player is (Minecraft does), and does not
take a user who is already playing. Nobody is asked on Android: the current user is given, or,
when the game excludes that one, the first other user it allows. So a second local player
needs a second user: create one in Profile Manager before the game.

The log says which controllers are connected, which player each one drives and how, each
player's connection and controller type, the controller types and players the game takes, and
what the user selection was asked and gave.

## QLaunch and a Switch 2 firmware

QLaunch boots the home menu of an installed firmware. Hokkaido takes a Switch firmware, whose
home menu is title `0100000000001000`. A Switch 2 keeps its system titles under the same numbers
with `04` in front (its home menu is `0400000000001000`), and its files open only with a
Switch 2's keys. LoopNative keeps the two side by side, from your own consoles:

- **Install keys** keeps a second key file. A file named `prod.keys` no longer replaces the
  installed one: the file with the base keys stays `prod.keys` and the other is kept as
  `prod.<hash>.keys`. Every other `*.keys` file in the keys folder is a key set too, and content
  that the main set cannot open is tried with each.
- **Install firmware** tells the console from the first file that opens and replaces only that
  console's files. When the installed keys open none of the files it changes nothing and says
  why: no key file is of that firmware's kind of console (the files' headers do not open), or
  the keys are older than the firmware (the key number it needs and the newest one installed),
  with the first file and what each key file reads its header as.
  The files are the `.nca` entries of the archive's root or of a folder inside it (other entries, a
  readme say, are left out, and one kept as a folder of parts is taken as it is); an archive with
  none says what it holds instead.
- **QLaunch** boots the Switch 2's home menu when its firmware is installed, else the Switch's.
  With both, the applet list has an entry for each.
- The home menu's own applets are looked for under `04`, a Switch 2 program is told its own
  firmware's version, and the three `ns` calls a 23.5.0 home menu makes at start are answered
  (from Eglantine's `fix/qlaunch-ns-3250-3251`).
- **Games from the home menu** (ported from [Orchid](https://github.com/MythosOfs/orchid)'s
  "Launch Switch 2 applications from the native Home Menu"): the Switch 2 home menu lists the
  games of the game list and starts the one you pick, as a process of its own. The app registers
  every game its scan finds (NCA, NSP and XCI as before, and now NUP containers and loose
  dumps by their file), and scans before the home menu boots if no scan has filled the list
  yet. To play a game again, exit it and pick it again in the home menu; restart and program
  switching from inside a game are not available for games started this way (Orchid's limit).
  Orchid's `.nx` containers are not supported here, and add-on content inside a NUP is not
  mounted for these launches.

None of this has run with a Switch 2 firmware: whether the files open with this reader, and
how far the home menu gets, a log of a first attempt will say (`System titles:` and `Keys:` lines
at start, `Firmware files:` lines at an install, `Home menu:` lines at a launch).

## Reporting problems

Include your phone model, Android version and GPU driver, the game, the engine, the LoopNative
report, and `Android/data/dev.loopnull.loopnative/files/log/eden_log.txt` (its `LoopNative:`
lines show what LoopNative did). When the app closed by itself, the last lines say where: an
exception nothing caught leaves a `Host crash:` line. Check logs for personal information before
sharing. Never share games, keys, firmware or account data.

### Diagnostic switches

Android can't pass environment variables to an app, so the app reads them from
`Android/data/<app id>/files/env.txt`, one `KEY=VALUE` per line (`#` starts a comment). They take
effect the next time the app starts. The available switches are listed in
[AGENTS.md](AGENTS.md#debugging-tools). Leave the file empty for normal play.

A wrong picture needs no file: set Settings, Debug, "Debug knobs" to 1 and play for half a
minute. Frames 300 and 1500 of the session are captured, then one every ten seconds for two
minutes (stay on a screen for twenty seconds to have it captured), and the log gets a
`Frame dump:` line for every pass, every texture a pass samples and the image shown, with each
one's size, format and lowest, highest and mean values, a map of where a large picture is
black, and a line for each clear, copy and upload. A cube or a small array is read layer by layer, and for
those of 2 to 16 layers the log also says what guest memory held for each layer at every upload,
which images were found where one was made, and where DMA copies into one went. Set it back to
0 afterwards: a capture stalls the game for a moment.

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

Launching games from the Switch 2 home menu is ported from
[Orchid](https://github.com/MythosOfs/orchid) by MythosOfs (GPL-3.0), a sibling fork of Eglantine.

LoopNative Pack is [Banana Native](https://git.eden-emu.dev/moceure/BananaNative) by moceure and
its contributors (MIT licence, [src/android/banana/LICENSE](src/android/banana/LICENSE)), based on
[nxnative](https://github.com/seedds/nxnative) by seedds, embedded the way Hokkaido embeds it. Its
frame generation uses Lossless Scaling's LSFG through Eden's port (GPL-3.0).

See [LICENSE.txt](LICENSE.txt) and the source files for the applicable license terms.
