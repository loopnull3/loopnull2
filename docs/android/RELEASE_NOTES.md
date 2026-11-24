# LoopNative 0.1.0

The first LoopNative build: Hokkaido android-0.7.1 with Banana Native removed and the LoopNative
engine in its place, tuned for the Galaxy Z Flip5 (Snapdragon 8 Gen 2, Adreno 740, 8 GB).

**Not yet tested on a Flip5.** See [LOOPNATIVE.md](../../LOOPNATIVE.md) for the honest status.

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
- Korean translation of everything LoopNative shows.

## Removed

- Banana Native: its module, its downloaded libraries, its screens and its scripts.

## App

- App ID `dev.loopnull.loopnative`, name LoopNative: it installs next to Hokkaido.
