# CLAUDE.md – Übergabe für Claude Code (PS-Vita-Port von Pokémon Mystery Dungeon: Red Rescue Team)

Diese Datei liest Claude Code beim Start automatisch. Sie fasst zusammen, was für die
Weiterarbeit am **PS-Vita-Port von PMD Red** nötig ist. Vorbild sind die Ports von
Zelda: The Minish Cap (`zelda-tmc-vita-port`) und Metroid: Zero Mission (`mzm-vita`).

## Zusammenarbeit mit Mike
- Sprache: **Deutsch**. Kurz, ehrlich, ohne Schönfärberei; Widerspruch und eigene
  Einschätzung sind erwünscht. Sparsam mit Tokens; bei Hintergrundarbeit erst melden,
  wenn sie fertig ist.
- Claude schreibt den Code. Mike testet auf seiner **PS Vita Slim PCH-2016 (HENkaku)** und
  schickt `ux0:data/pmd/log.txt` (+ `log_prev.txt`), Screenshots, Spielstand (`pmd_red.sav`)
  und Schnellspeicherungen.
- Mike nutzt Linux (Raspberry Pi 5), kein Windows.
- „Breitbild“ heißt bei Mike fast immer die **Weitsicht** (Full View, 400×240).
- Ziele: Weitsicht möglichst immer, konstanter Zoom, konstante 60 FPS (sonst stabile 30),
  Komfortfunktionen, Cheat-Menü, Touch/Rückseite. Versionsnummer in der App sichtbar
  (LiveArea-Bilder tragen sie, Port-Menü zeigt sie).

## Git-Regeln (wichtig)
- Repo: `https://github.com/bit-wmike/pmd-red-Vita`, Branch **`master`** (Upstream pret/pmd-red).
- **Nur in Mikes eigene Repos pushen** (origin). Nie zu pret oder anderen fremden Repos.
- Commit-Autor: `bit-wmike <295828+bit-wmike@users.noreply.github.com>` – keine private
  Mailadresse, keine Passwörter/Accounts/Adressen ins Repo.
- Commit-Nachricht: `Vita x.y.z: <kurze Zusammenfassung>` + Stichpunkte (Englisch).
- Keine ROM-Daten und kein Nintendo-Artwork committen (`platform/vita/sce_sys_local/` und
  `build/` sind git-ignoriert; die Decomp-Grafiken/-Daten des Upstream-Repos bleiben, wie sie sind).

## Projektaufbau
- Basis: pret/pmd-red (Decomp der US-ROM, SHA-1 `9f4cfc5b5f4859d17169a485462e977c7aac2b89`,
  32 MiB). Der Port liegt in `port/`, `platform/`, `tools/port/`; Änderungen am Spielcode
  stehen unter `#ifdef PORT` (Header `include/gba/{io_reg,defines,macro}.h`, `src/main.c`,
  `src/cpu.c`, `src/code_800D090.c`, `src/reg_control.c`, `src/m4a.c`, `src/sprite.c`,
  `src/dungeon_vram.c`, `src/ground_main.c`, `src/ground_sprite.c`, Culling-Stellen u. a.).
- Build immer mit `-DNONMATCHING` (C-Varianten statt agbcc-Tricks) und ohne
  `agb_flash*.c`/`libisagbprn.c` (ersetzt durch `port/host_flash.c`, `port/host_stubs.c`).
- `port/`: `host_core.c` (Frame-Loop: Rendern, HBlank-DMA, VBlank-Interrupt, Audio, Tasten),
  `host_mem.c` (GBA-Speicher als Host-Arrays; Spielvariablen liegen in den Sektionen
  `ewram_data`/`iwram_data`/`ewram_init`/… mit `__start_`/`__stop_`-Symbolen),
  `host_dma.c`, `host_bios.c`, `host_flash.c` (128 KiB Flash als Datei), `host_audio.c`
  (nimmt pro Bild den gemischten m4a-Block aus `SoundInfo.pcmBuffer`, PSG-Emulation),
  `host_assets.c` (ROM-Lader), `port_view.c` (Weitsicht), `port_game.c` (Schnellspeichern),
  `port_cheats.c`, `port_config.c`, `asm_rt.h` (Laufzeit für übersetzten Assembler).
- `port/ppu/`: Software-PPU (VirtuaPPU-Ableger aus dem mzm-Port; `virtuappu_mode1_*`-API:
  `bg_dx/dy`, `win_dx/dy`, `line_dy`, `obj_port`/`obj_base_x/y`/`obj_dx/dy`, Shadow-Tilemaps
  `ws_shadow[]` mit `ws_full_view`; Eintrag `0xFFFF` = durchsichtige Zelle (Port-Erweiterung)).
- `platform/host/main_host.c` (headless Testläufer), `platform/vita/source/main_vita.c`
  (vita2d, Audio-Thread, Menü, settings.ini, log), `platform/vita/vita.ld` (Linker-Skript
  mit Lücke vor dem Datensegment, wie beim TMC-Port), `platform/vita/tools/`
  (`bootstrap_vitasdk.sh`, `make_livearea_art.py` – eigenes Artwork mit Versionsnummer).
- `tools/port/`: `gen_data.py` (Assembler-Datendateien → leere Arrays + Relocations +
  Hashes), `pp_assets.py` (INCBIN in C → leere Arrays + Registrierung; entfernt auch
  `asm("rN")`-Register-Hacks), `keysel.py`, `asm2c.py` + `gen_m4a.sh` + `extract_naked.py`
  (m4a_1.s, arm_funcs.s und `sub_80A5204` aus ground_map.c → C, bitgenau geprüft mit
  `test_m4a_diff.sh` / `test_ground_map_diff.sh` unter qemu-arm), `make_graphics.sh`,
  `fake_rom.py` (Pseudo-ROM aus den Repo-Daten für Tests ohne echte ROM).

## Spieldaten aus der ROM (Kernidee)
Das Binary enthält keine Spieldaten. Jeder Datenblock (Sektion einer `data/*.s`-Datei,
jedes INCBIN-Array in C) wird beim Build mit Größe, FNV-64-Hash (Zeigerfelder und
`baserom.gba`-Teile als Löcher genullt) und zwei 8-Byte-Suchfenstern registriert.
Beim Start sucht `host_assets.c` die Blöcke in der ROM (4-Byte-Raster, dann Byte-Raster),
kopiert sie in die Arrays, patcht die Relocations (`gPortRelocs`) und schreibt
`romindex.txt` (Cache, geprüft gegen ROM-SHA-1 + Version). Konstante Blöcke werden gefüllt,
nicht gesucht. Die ROM bleibt im Speicher (`gHostRom`, `Port_RomPtr()` für rohe
GBA-Zeiger in unextrahierten Daten – bisher nicht nötig).

## Bauen
```sh
export VITASDK=/usr/local/vitasdk PATH=$VITASDK/bin:$PATH   # Bootstrap: platform/vita/tools/bootstrap_vitasdk.sh
make -f Makefile.port TARGET=vita vpk -j4   # build/port-vita/MysteryDungeonRed_Vita_<VERSION>.vpk + pmd_vita-<VERSION>.elf
make -f Makefile.port TARGET=host -j4       # x86 32-Bit headless (gcc-multilib, pyelftools, arm-none-eabi-binutils)
make -f Makefile.port TARGET=armhost -j4    # ARMv7, läuft unter qemu-arm (arm-linux-gnueabihf-gcc)
```
- Version in `platform/vita/VERSION`; zu jeder VPK die ELF `pmd_vita-<version>.elf` mitliefern.
- Cloud-Hinweise: `gcc-arm-linux-gnueabihf` und `gcc-multilib` schließen sich per apt aus –
  erst multilib, dann den Cross-Compiler installieren und `ln -s x86_64-linux-gnu/asm
  /usr/include/asm`. Nie `pkill -f` mit einem Muster, das die eigene Shell trifft.
- **Vita-GCC 15 ist strenger** → vor jeder Version `TARGET=vita` bauen. Der Präprozessor-Schritt
  braucht `-std=gnu11` (sonst `nullptr`-Fehler aus `stddef.h`).

## Testen (Host)
`build/port-host/pmd_host rom.gba <frames> <ausgabeordner> [skript]`, Skriptzeilen
`<frame> <tastenmaske-hex>` (A=1, B=2, Select=4, Start=8, rechts=10, links=20, oben=40,
unten=80, R=100, L=200; Tasten bleiben bis zur nächsten Zeile gedrückt).
Schalter: `PMD_SHOT=f1,f2` / `PMD_SHOT_EVERY=n` (PPM), `PMD_VIEW=0/1/2`, `PMD_WAV=1`,
`PMD_QSAVE_AT`/`PMD_QLOAD_AT`, `PMD_CHEATS=1`, `PMD_30FPS=1`, `PMD_TIMING=1`,
`PMD_VIEW_DUMP=frame` (OAM/Ansicht), `PMD_AUDIO_DEBUG=1`.
- Ohne echte ROM: `python3 tools/port/fake_rom.py build/fake.gba` (nach einem Build) – das
  Spiel läuft damit bis in die Spielwelt (Intro-Skript, Basis), Musik ab ~Frame 968;
  `baserom.gba`-Teile (u. a. PCM-Samples) sind darin Nullen.
- Prüfregeln vor jeder Version: (1) Host x86 und armhost (qemu) liefern **byteidentische
  PPMs** (`cmp`); (2) Renderer-Änderungen Bild für Bild gegen die alte Version vergleichen;
  (3) Vita-Build ohne Fehler.

## Stand (0.1.1, 04.11.2026)
- 0.1.1: Weitsicht/Breitbild als **konstantes Bild in allen Szenen** (Szenen ohne Karte:
  GBA-Bild mittig, Ränder schwarz; `sContentW/H` = Kartenanteil), Cheats nur noch
  mechanik-schonend (Erfahrung ×1–4 in `AddExpPoints`, kein Hunger in
  `dungeon_turn_effects.c`), Vita-Tasten: Viereck = L+R, Dreieck = L+A, rechter Stick =
  R+Richtung, Fronttouch halten = Schnellvorlauf (alles im Menü umstellbar).
- Host-, armhost- und Vita-Build laufen; auf der Pseudo-ROM bootet das Spiel durch Logos,
  Titel und Intro bis in die Basis (Weitsicht 400×240 in der Basis geprüft, ARM = x86).
- Weitsicht: Szenen-Hooks (`Port_ViewSceneFrame`) in Dungeon-/Ground-/Weltkarten-/
  Freundesareal-Frame; Kartenebenen (BG3, BG2 bei Zweischicht-Karten) werden vom Port aus
  `gDungeon`-Cels, `chunkMappings`/`tileMappings` bzw. den 64×64-Bildern im RAM gefüllt; BG0/BG1 (Text/HUD/Dunkelheit) am GBA-Rechteck,
  außerhalb leer bzw. Dunkel-Kachel 0x27A; Sprites über `Port_SpriteStaged`/`Port_OamSlot`
  mit erweitertem Culling (`gPortViewMargin*`); WIN/HDMA um (dx,dy) verschoben.
- Audio: m4a-Engine übersetzt, PCM-Block pro Bild, PSG emuliert (Musik hörbar im Host-Lauf).
- **Noch nicht auf echter Hardware/ROM getestet** – Mike muss die US-ROM nach
  `ux0:data/pmd/` legen und `log.txt` schicken.

## Offene Punkte (nächste Schritte)
1. Test mit Mikes ROM auf der Vita: Startzeit (ROM-Scan), Leistung (`[perf]`), Weitsicht in
   echten Dungeons (Dunkelheit in den Rändern: oben/unten nutzen Zeile 0/159 der WIN1-Tabelle;
   Rasteranzeige/Effekte auf BG2 bleiben auf das GBA-Rechteck beschränkt).
2. Weltkarte/Freundesareal-Karte sind in der Weitsicht (480×320-Bild), aber ungetestet
   (der Pseudo-ROM-Lauf erreicht sie nicht); Menüfenster darauf prüfen.
3. Dungeon: `ShouldDisplayEntity` (±32 px) gilt auch für Logik – Monster/Items in den Rändern
   jenseits davon werden nicht gezeichnet; evtl. reine Render-Kopie einführen.
4. Leistung messen (Vita: 400×240 Software-PPU mit 3 Threads; Old-3DS-Profil aktiv).
5. Weitere sanfte Cheats (z. B. IQ-Multiplikator, Geld ×2) nur nach Rücksprache; Overlay/Karte
   wie beim TMC-Port.
