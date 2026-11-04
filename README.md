# Pokémon Mystery Dungeon: Red Rescue Team – PS Vita port

A native PlayStation Vita port of *Pokémon Mystery Dungeon: Red Rescue Team*,
built from the [pret/pmd-red](https://github.com/pret/pmd-red) decompilation.
The game code runs natively; the GBA hardware (memory, DMA, interrupts, sound
FIFO, picture unit) is reproduced in C.

German setup guide: [`platform/vita/README.md`](platform/vita/README.md).
The original decompilation README is in [`docs/README-decomp.md`](docs/README-decomp.md).

**No game data is included.** The port needs your own dump of the US ROM
(`sha1: 9f4cfc5b5f4859d17169a485462e977c7aac2b89`). At start-up it reads
graphics, maps, text and sounds from that file (the first start scans the ROM
once and caches the offsets in `romindex.txt`).

## Features

- Wide view (282×160) and full view (400×240, default, like the Minish Cap and
  Zero Mission ports) with a fixed zoom in dungeons and in the towns/base
  scenes: the port draws the map around the GBA screen from the game's own map
  data, the HUD and text boxes stay where they are.
- In-game menu (START + SELECT): view, display style (smooth / sharp /
  pixel), 30 fps mode, fast forward on Triangle, rear touch quick save/load,
  cheats (leader HP, belly, money). Settings in `settings.ini`.
- Quick save / quick load (a snapshot of the whole game state).
- Frame pacing that skips drawing a frame instead of slowing the game down.
- Saves (`pmd_red.sav`, 128 KiB) are compatible with GBA emulators.

## Building

Needs [VitaSDK](https://vitasdk.org) (`platform/vita/tools/bootstrap_vitasdk.sh`
builds one from source), `arm-none-eabi` binutils (to assemble the game's
data files once), python3 with `pyelftools`, and libpng for the decomp's
graphics converter.

```sh
make -f Makefile.port TARGET=vita vpk     # MysteryDungeonRed_Vita_<version>.vpk
make -f Makefile.port TARGET=host         # 32-bit Linux test runner (headless)
make -f Makefile.port TARGET=armhost      # ARMv7 test runner for qemu-arm
```

The build itself does not need the ROM. The host test runner does
(`build/port-host/pmd_host rom.gba [frames] [outdir] [script]`, see
`platform/host/main_host.c` for its options). `tools/port/fake_rom.py`
builds a synthetic image from the repository's data for testing without a
cartridge dump.

## Layout

| Path | Contents |
|------|----------|
| `port/` | GBA hardware layer, view, settings, quick save, cheats |
| `port/ppu/` | Software GBA PPU (from VirtuaPPU, GPL-3.0) |
| `platform/vita/` | Vita front end (vita2d, audio, input, menu) |
| `platform/host/` | Headless Linux runner for automated tests |
| `tools/port/` | Build tools: data/INCBIN registration, asm→C translator |

Game source changes for the port are marked with `#ifdef PORT`.

## License

- Decompilation: see [`docs/README-decomp.md`](docs/README-decomp.md) (pret).
- Port code (`port/`, `platform/`, `tools/port/`): GPL-3.0-or-later, because
  `port/ppu/` (GPL-3.0-or-later, see [`port/ppu/LICENSE`](port/ppu/LICENSE))
  is linked into every build. The complete source is in this repository.
- Vita libraries: see [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md).

Pokémon and Pokémon Mystery Dungeon are trademarks of Nintendo / Creatures /
GAME FREAK / The Pokémon Company / Chunsoft. This project is not affiliated
with or endorsed by them.
