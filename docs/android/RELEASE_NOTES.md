# LoopNative 0.1.0

The first LoopNative build: Hokkaido android-0.7.1 with the LoopNative engine added and Banana
Native modified into LoopNative Pack, tuned for the Galaxy Z Flip5 (Snapdragon 8 Gen 2, Adreno 740, 8 GB) and
the Galaxy S26 / S26+ with Exynos 2600 (Xclipse 960, 12 GB).

**Not yet tested on a Flip5 or an S26.** See [LOOPNATIVE.md](../../LOOPNATIVE.md) for the honest status.

## New

- **LoopNative engine** (Game properties › Engine): the emulator core started with the phone's
  and the game's tuning. Auto picks it for profiled games and on 8 GB phones.
- **Guest memory on storage:** a game's RAM in a sparse file on internal storage instead of in
  RAM, for games larger than the phone's memory (Ocarina of Time, Mario Kart World).
- **Thermal governor:** 30 fps before the phone throttles, every other frame skipped when very
  hot, the cap lifted once it has cooled.
- **Prepare and the LoopNative report:** reads each module of the game's code once (LZ4 and
  ZBIC), finds its NVN entry point and the instructions a native runner intercepts, and shows how
  the game fits this phone.
- **Title profiles** for Ocarina of Time, Mario Kart World, Pokémon Pokopia, Fire Emblem:
  Fortune's Weave, Donkey Kong Bananza and Star Fox.
- **Galaxy S26 Exynos profile:** 0.5x-0.75x renders upscaled with AMD FSR, a custom RADV driver
  recommended, 12 GB memory planning, a thermal governor tuned for the S26.
- **Upscaling by GPU maker:** AMD FSR on Xclipse, Qualcomm SGSR on Adreno, whenever LoopNative
  renders below 1x.
- **Smooth motion (optional):** FSR 3 frame generation per game.
- **Build all shaders now (Hokkaido engine)** in Game properties: the emulator's shader precache
  on Android. The game's compute shaders are found in its files once; each start then builds
  them, and the graphics pipelines saved from earlier play, on the loading screen.
- **Auto-map two Joy-Cons** (Settings › Controls › a player): the left half of a player's
  controls from one controller and the right half from another, for a pair of Joy-Cons, which
  Android lists as two controllers. Controller ports no longer depend on the order the
  controllers connected in.
- **A second player:** a controller in no mapping becomes the next free player at a button
  press during a game. When a game asks which user a player is (Minecraft, at + on the second
  controller), a list asks who is playing and the game gets the pick (create a second user in
  Profile Manager first).
- **Cache storage size:** a game that asks how large the cache storage it created is gets
  that size, not zero (Minecraft asks at start).
- **Vibration goes to the controller** when it has a motor; the phone vibrates in its place
  only for a controller without one.
- Korean translation of everything LoopNative shows.

## LoopNative Pack (Banana Native v0.3.51, modified)

- **Banana Native v0.3.51** in place of v0.3.45: games do not wait for shaders they can play
  without, the RAM saver's level reaches the texture cache, the depth bounds test and indirect
  quad draws work, and the in-game menu is built on its new drawer. Built shaders stay; a game's
  code is rewritten once at its next start.
- The engine that rebuilds a game's files: Prepare rewrites its code and translates its shaders
  into a pack, then runs the code natively with NVN on Vulkan.
- **8 GB class** (Galaxy Z Flip5): RAM saver: Strong, less vegetation, memory pools placed in
  256 MiB parts and a 1280x720 render from a game's first start.
- **Shaders before the first start:** a prepared game without shaders asks once whether to build
  them all first, so its first sessions don't stutter while each shader is built.
- **Memory class** for phones of 12 GB or less (LoopNative Pack options › Memory class): memory
  pools are placed in 256 MB parts; the texture cache keeps measuring its own limit. On a 12 GB
  phone Auto then starts games on the pack whose profile asks for more RAM (Mario Kart World).
- **Auto by phone:** the pack only where the profile's required RAM fits and, on Xclipse, a RADV
  driver is installed; Ocarina of Time on the Galaxy S26 with RADV.
- Game properties › **LoopNative Pack options**, **Import a Banana Native pack**, **Build all
  shaders now**.
- **Diagnostic switches** (in LoopNative Pack options): Banana Native switches for one game, to
  test a change on the phone without another build.
- **White picture and white blocks fixed** (Mario Kart World, Ocarina of Time on RADV): the shader
  tools write min / max / clamp with the Switch's NaN results.
- **Pink textures:** ASTC blocks that hold no ASTC data become transparent instead of pink; a
  texture read while the game rewrites it keeps its last whole content.
- A constant depth bias is kept where a D24 target is made D32F.
- **Mario Kart World** stopping after a race, before the result movie (upstream saw it in about
  one race in five): Banana Native's fix from its v0.3.52 preview is in. Not yet run on a phone.
- Shaders built before these fixes are dropped once and built again while you play.
- The game hosts are built from Banana Native's source with these patches.

## App

- App ID `dev.loopnull.loopnative`, name LoopNative: it installs next to Hokkaido.
