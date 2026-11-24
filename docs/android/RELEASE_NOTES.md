# Hokkaido 0.7.1

An update to 0.7.0's two engines: Banana Native v0.3.45, game options you can now change from Hokkaido, smoother Pokémon Pokopia on the Hokkaido engine, and memory and stability fixes from Lichii.

Tested on a Samsung Galaxy S26 (Exynos 2600, Xclipse 960, 12 GB), a Galaxy S24 (Exynos 2400, Xclipse 940, 8 GB) and a Galaxy S25 (Snapdragon 8 Elite, Adreno 830, 12 GB), all on Android 17.

**Updating:** install the APK over 0.7.0 or 0.6.x. Your games, saves, settings and Banana Native packs stay. The first start of each Banana Native game after the update rewrites its code once more (a short Prepare, as Banana Native v0.3.45 asks); your shaders are kept.

## Banana Native v0.3.45

Banana Native inside Hokkaido is now v0.3.45 (from v0.3.41). From its v0.3.42 to v0.3.45 releases:

- **Pokémon Pokopia:** native 4:3 UI and rendering for v2.0.1.
- **Donkey Kong Bananza:** the gameplay camera fills screens wider than 16:9, and its dynamic resolution snaps to the game's own sizes.
- **Mario Tennis Fever:** a frame cap that keeps the game at full speed.
- **Engine:** host services and NVN fixes from nxnative (account, file system, Mii, fonts, time, amiibo), wait timeouts and packer shader checks, and fixes in the game code rewriter.
- **Shaders:** compute shaders are built in the background, and shader compiles are shorter for shaders built from now on (mostly Pokopia).
- **Memory:** a second RAM saver level, **RAM saver: Strong** (see Banana Native options below).
- **In game:** **Clean picture**, one tap that hides the controls and overlays, in Quick settings › General. More screen scaling choices.
- **Audio:** keeps playing when another app makes sound.
- **Prepare** works on exFAT, SD card and USB storage.
- v0.3.45 itself fixes v0.3.44's crash at boot in every game.

## New: Banana Native options in Game properties

The game options Banana Native keeps on its own settings page could not be changed from Hokkaido. A game's **Game properties › Banana Native options** now has them. Changes take effect at the game's next start.

- **Full vegetation:** all grass and foliage. Off: less of it, and less GPU work. (The 0.7.0 notes said this was in Quick settings; it was not. It is here now.)
- **RAM saver:** keeps texture data in a file instead of RAM, so the game needs less memory. In Ocarina of Time it can leave holes in the terrain.
- **RAM saver: Strong:** even less memory: the two finest texture levels left out, a smaller texture cache and one background pipeline at a time. It includes the RAM saver.
- **Soft textures:** large textures at a lower resolution, so the game needs less memory.

## Banana Native in Hokkaido

- **Older shaders keep playing.** When Banana Native's shader translator changes, a game whose existing shaders still work now starts with them, as in Banana Native's own app. Before, Hokkaido sent such a game to a Prepare that translated all its shaders again (Ocarina of Time: more than 40,000).
- **New: Build all shaders now** in Game properties. Games that build their shaders while you play (Ocarina of Time, Mario Kart World and the other Switch 2 games) show white or missing parts in new scenes until those shaders exist. This translates the whole set at once, in Hokkaido's progress screen; your game code is kept. Thanks to loopnull3 (#25).
- **The right copy of a game starts.** With two copies of one dump (for example an incomplete one prepared first), a start read whichever copy Banana Native had seen first, and could stay on a black screen. It now uses the dump you start. Thanks to loopnull3 (#25).
- **Try anyway packs start.** A pack made with Try anyway for a game version Banana Native has not tested now starts after Banana Native's warning, instead of asking to prepare the same pack again. Thanks to loopnull3 (#25).
- **No clash with Banana Native's own app.** Both apps kept their Prepare job in the same folder next to the game (`<title ID>-work`), so with both installed each one could resume or overwrite the other's job. Hokkaido now uses its own folder, `<title ID>-hokkaido-work`. A Prepare that 0.7.0 began in the old folder finishes there.

## Hokkaido engine

- **Pokémon Pokopia's frames are even.** It runs a frame ahead, and when its GPU fell behind, Hokkaido waited for two frames, which paired its frames: one quickly after another, then a long gap, so walking and turning the camera juddered. It now waits for one frame, so frames come evenly. It still runs slower on the Galaxy S26 than on the S24; Banana Native is the faster engine for it.
- **Memory fixes from Lichii:**
  - The texture cache's cleanup stopped freeing memory once a few compressed (ASTC) textures it could not evict sat at the end of its list; it now skips those and frees the rest, and old textures give their GPU memory back (#1731).
  - Buffers used for many small uploads are released after a while unused.
  - Two GPU address space leaks that could run long sessions out of space are fixed (#1911).
- **Stability fixes from Lichii:**
  - A freed index buffer could still be used (#2163).
  - Texture and sampler lookups are bounds-checked (#2235), and when the GPU driver refuses a new sampler, which happens on Adreno and Mali, a cached one is used instead of crashing (#2236).
  - The pipeline cache is written atomically, so Android closing the app mid-write cannot corrupt it (#2100).
  - Command lists the GPU writes are read from a current copy (#2007), and DMA copies keep the texture cache up to date.
- **Less CPU work on the GPU thread:** command list copies are submitted in batches (#2006), and a frequent GPU tick check is cheaper.
- **Occlusion queries** are timed from the frame that recorded them (#1809).

## Getting started with Banana Native

1. **GPU driver.** On an Xclipse GPU (Exynos), install a custom RADV driver in Hokkaido's GPU driver manager, globally or for the game. Banana Native uses the same driver as Hokkaido.
2. **Engine.** Open the game's Game properties › **Engine**. Auto already picks Banana Native for its playable games (Donkey Kong Bananza, Pokémon Pokopia, Mario Kart World, Fire Emblem: Fortune's Weave, Star Fox); for the others choose **Banana Native (experimental)**.
3. **First start: Prepare.** Banana Native rewrites the game's code from your dump once. It takes a few minutes, uses the CPU hard and warms the phone, so do it on the charger. You can hide it, pause it, or close Hokkaido; it continues where it stopped.
4. **Play.** Shaders are built while you play and kept, so the first sessions show missing objects and pop-in that go away. Or use **Build all shaders now** in Game properties.
5. **In game,** Back or the controller's menu button opens Hokkaido's menu: pause, controller, Quick settings with all of Banana Native's in-game settings, overlay, exit.

## Moving from the Banana Native app

If you already prepared a game in Banana Native's own app, you don't need to prepare it again:

1. In the Banana Native app, open the game's **Manage** sheet and choose **Export pack…**. It writes `banana-pack-<title ID>-<version>.zip` to a folder you pick.
2. In Hokkaido, open the same game's **Game properties › Import Banana Native pack** and pick that zip.
3. Hokkaido copies the pack and checks it with Banana Native's own checks: it must be this game's, and its code must fit your dump (the same game version). If it doesn't fit, nothing changes and the reason is shown.

An exported pack has no shaders built while you played (Banana Native leaves them out of exports); the game builds them again while you play. Saves don't move automatically yet: Banana Native's save backups and Hokkaido's save import use different zip layouts.

## Known issues

- **Banana Native needs a custom RADV driver on Xclipse phones.** Samsung's own driver has no placed memory maps, so a Banana Native game shows a black screen with sound there.
- **Ocarina of Time on Banana Native:**
  - On 12 GB phones it uses most of the memory; RAM saver: Strong helps, but the RAM saver can leave holes in the terrain. Some textures in the distance can show pink.
  - In the Great Deku Tree, the pillars a switch should move were reported not to move (issue #28); we are looking into it.
- **On the Hokkaido engine:**
  - Fast Fusion runs very slowly and keeps the GPU so busy that the whole phone responds slowly.
  - Pokémon Pokopia runs slower than in earlier versions on the Galaxy S26.
  - Ocarina of Time can stay on a black screen after its intro movie on some Snapdragon phones (issue #12).
- **Vulkan 1.3 is required.** Phones whose stock driver reports an older version need a newer driver (for example Turnip on Adreno). On Mali, players report that faux123's PanVK driver draws Fire Emblem: Fortune's Weave, where the stock driver shows black (issue #22).
- Microphone input is not supported on Android.
- The phone heats up during long sessions and slows down until it cools.

## For testers

- Install the APK, add your games folder, and set up keys and firmware as usual. None are included.
- **Hokkaido engine:** Handheld mode, NCE CPU backend, 0.5x resolution, and a custom RADV driver on Xclipse GPUs.
- **Banana Native engine:** a custom RADV driver on Xclipse GPUs, and Prepare on the charger.
- Diagnostic switches can be set in `Android/data/dev.lichii.android/files/env.txt` (see the README).
- Report problems at https://github.com/pavelgajdo/Hokkaido/issues with your phone, driver, the engine and the log: `eden_log.txt` for Hokkaido, or the game's newest log from `Android/data/dev.lichii.android/files/logs/` for Banana Native.

## Thanks

loopnull3 for the Banana Native fixes and Build all shaders now (#25). The Lichii contributors whose fixes are ported here: Zortos and moonzzeyy. Banana Native is by moceure and its contributors (MIT licence), based on [nxnative](https://github.com/seedds/nxnative) by seedds. Its frame generation uses Lossless Scaling's LSFG through Eden's port (GPL-3.0).
