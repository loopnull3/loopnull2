# LoopNative 0.1.0

The first LoopNative build: Hokkaido android-0.7.1 with Banana Native removed and the LoopNative
engine in its place, tuned for the Galaxy Z Flip5 (Snapdragon 8 Gen 2, Adreno 740, 8 GB) and
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
- Korean translation of everything LoopNative shows.

## Removed

- Banana Native: its module, its downloaded libraries, its screens and its scripts.

## App

- App ID `dev.loopnull.loopnative`, name LoopNative: it installs next to Hokkaido.
