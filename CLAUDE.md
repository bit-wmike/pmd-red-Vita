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
- „Breitbild“ heißt bei Mike fast immer die **Weitsicht** (Full View, 424×240).
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
- Makefile.port kennt keine Header-Abhängigkeiten: nach Änderungen an `CPPFLAGS`, `port/*.h`
  oder `port/ppu/include/*` das Build-Verzeichnis (`build/port-<target>`) löschen.
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

## Stand (0.1.6, 05.11.2026)
- 0.1.6: **Spieltext in Deutsch/Französisch/Italienisch/Spanisch aus der EU-ROM** (`port/port_text.c`).
  Der US-Code liefert englische Strings (Binary + Datenblöcke); `Port_Text(s)` ersetzt sie zur
  Laufzeit über einen Index aller Strings der ROM (Inhalt → erste Fundstelle). Drei Ablageformen
  in der EU-ROM: (1) **Gruppen** EN,DE,FR,IT,ES hintereinander (Skripte, Sätze; nur EN hat einen
  Zeiger – Erkennung: Folgestrings ohne Zeigerstelle, Wortüberlappung < 50 %); (2) **parallele
  Zeigertabellen** (Namen, Beschreibungen, Kampfmeldungen): fünf Tabellen am Stück
  (Monsternamen; Erkennung über identische IT/ES-Namen oder Sprache der Teiltabelle) oder
  Tabelle/Strings/Tabelle/Strings (Items, Attacken; `ParallelAt`: exakt gleich lange Zeigerläufe,
  Sprache per Wortliste/Sonderbuchstaben, sonst Reihenfolge EN,DE,FR,IT,ES – Achtung, manche
  Tabellen haben EN,FR,DE); (3) **Pools** `{u16; Text; 0; Pad}` je Sprache in gleicher Reihenfolge
  (Menüs, Quiz, Systemtexte; Pools gleicher Größe + Sprache werden gepaart, ein in den nächsten
  Sprachblock durchlaufender Pool wird per Größen-Defizit geteilt); (4) **Code-Datenregionen**
  (Dungeon-Meldungen aus `dungeon_strings.c`: kleine Varianten-Zeigertabellen + Strings; je
  Sprache eine Region, die k-te Zeigerstelle entspricht sich; Regionen = zusammengefasste Pools
  mit Zeigerkopf, Paarung nur bei gleicher Zeigerzahl – FR/ES weichen ab). Zeiger mitten in einen
  String (Seitenumbruch `#P`) → `PageOf`: ganze Übersetzung, gleiche Seite. Hooks: `FormatString`,
  `AppendString`, `DrawStringInternal`, `GetStringLineWidth`, `mini_vpprintf` (Format + `%s`),
  `GetMonSpecies`. Einmal übersetzte Strings (`sResults`) werden nicht erneut übersetzt.
  Test-Harness: Scratchpad `text_test3.c` (Strings → Übersetzung), `text_test4.c` (Herkunft).
  Host: `PMD_LANG=1`. Vita: Sprachwahl einmal beim ersten Start mit EU-ROM (`ChooseLanguage`),
  danach nur im Port-Menü („Sprache“); `settings.ini`-Schlüssel `language` (-1 = noch nicht gewählt).
  Bekannte Grenzen: geteilte Standardtexte (z. B. „Inflicts damage on the target.“ für viele
  Attacken) landen auf dem ersten Tabelleneintrag mit diesem Text → falsche Beschreibung;
  Abhilfe wären Hooks mit Index (GetMoveDescription o. ä.). Namen aus RAM (Spielername) könnten
  theoretisch übersetzt werden, wenn sie exakt einem ROM-String entsprechen.
  Außerdem 0.1.6: erster Lauf bis ins Dungeon (Kleinhain) auf dem Host – `dungeon_map_access.c`
  hatte ein hart kodiertes `0x06008000` (jetzt `VRAM + 0x8000`), vorher Absturz beim Betreten.
  `PMD_TEXT_DEBUG=1` (Host) protokolliert jeden Text ohne Übersetzung.
- 0.1.5: **EU-ROM läuft auf dem Host** (Mikes ROM liegt in der Cloud unter `/home/user/rom_eu/pmd_eu.gba`,
  SHA-1 `afee3b060dd5fd4a68afb1b003456aef3a2af073`; Logos, Titel, Intro, Quiz mit Text geprüft).
  Drei Ursachen für Schwarz-/Weißbild und Kästchen-Text:
  (1) Die EU-ROM speichert alle 178 Boden-Tilesets (`*.bpc`) **gepackt** (eigene Lauflängen-
  Kodierung mit zwei Farbregistern, Chunk-Tabelle als High-/Low-Byte-Strom; Format als
  Kommentar über `UnpackEuTiles` in `host_assets.c`). Der Lader entpackt sie beim Füllen der
  `gFile*c`-Arrays (175/178 byteidentisch zur US-Datei, S02c/S05c haben in der EU weniger
  Kacheln, T01P01c 3 andere Zellen). Referenz-Dekoder: Scratchpad `eufull.py`.
  (2) `keysel.py` erklärte Blöcke ohne 4-Byte-ausgerichtetes Suchfenster zu **Konstantblöcken**
  (Füllwert 0) – auch bei gemischtem Inhalt (z. B. Zeichentabellen-Zähler vor einem Zeiger, 593
  Blöcke!). Jetzt: unausgerichtete Fenster, sonst eingebettete Kopie (`fallback`) ohne Schlüssel.
  (3) `Verify` prüft in einem Zwischenpuffer; ein Fehlversuch löscht nicht mehr den Block
  (vorher Index-Lauf ≠ Frischlauf). Außerdem: Index-Puffer 4 MiB, Rückseiten-Touch komplett
  entfernt (Mike), ROM-Datei-Übernahme in `Port_ChooseFile` nur bei Zustand −1/2.
  Spieltext bleibt Englisch (Strings aus der Decomp); Deutsch bräuchte die EU-Stringtabellen.
- 0.1.4: Mikes erster Lauf (0.1.3, EU-ROM) stürzte in `LoadCharmaps` ab: `OpenFileAndGetFileDataPtr`
  lieferte NULL, weil das System-Archiv (`data/system_sbin.s`) in der EU-ROM nicht als Ganzes
  gefunden wurde (Core-Dump mit eigenem Parser `/home/user/dumps/parse_dump.py` + `addr2line`
  ausgewertet; `vita-parse-core` darf hier nicht ausgeführt werden). Abhilfe: (1) jeder
  Datenblock hat **Kind-Blöcke** je globalem Symbol (eine Datei/Tabelle je Kind, `children`
  in `PortBlob`); wird der Block nicht am Stück gefunden, werden die Kinder einzeln gesucht
  (exakt, dann teilweise); (2) `OpenFile` liefert unter PORT bei fehlender Datei eine leere
  Datei statt NULL und schreibt `file: not found in the game data: <name>` ins Log.
  LiveArea-Bilder aus Mikes Vorlagen liegen in `platform/vita/sce_sys_local/` (git-ignoriert,
  Nintendo-Artwork); die VPK wird damit gebaut, das Repo enthält weiter nur die Platzhalter.
- 0.1.3: Mikes ROM ist die **Europa-Version** (B24P). Der Port nimmt sie an (`Port_RomVersion`
  prüft den Spielcode im Header), sucht alle Blöcke per Inhalt und übernimmt abweichende
  Blöcke teilweise (Chunk-Hashes à 512 Byte, `TryPartial`, ≥ 50 % Treffer; Test:
  `fake_rom.py out.gba 1 7` verfälscht jeden 7. Block). Beim Start zeigt die Vita, wie viele
  Blöcke teilweise/gar nicht passten; `log.txt` nennt die ersten 40 Namen. Erster echter
  Lauf mit der EU-ROM steht aus – falls Schriften/Texte fehlen, Blocknamen aus dem Log prüfen.
  LiveArea-Grafik neu im TMC-Stil (Rettungsabzeichen, flach), Weitsicht 424×240.
- 0.1.1: Weitsicht/Breitbild als **konstantes Bild in allen Szenen** (Szenen ohne Karte:
  GBA-Bild mittig, Ränder schwarz; `sContentW/H` = Kartenanteil), Cheats nur noch
  mechanik-schonend (Erfahrung ×1–4 in `AddExpPoints`, kein Hunger in
  `dungeon_turn_effects.c`), Vita-Tasten: Viereck = L+R, Dreieck = L+A, rechter Stick =
  R+Richtung, Fronttouch halten = Schnellvorlauf (alles im Menü umstellbar).
- Host-, armhost- und Vita-Build laufen; auf der Pseudo-ROM bootet das Spiel durch Logos,
  Titel und Intro bis in die Basis (Weitsicht 424×240 in der Basis geprüft, ARM = x86).
- Weitsicht: Szenen-Hooks (`Port_ViewSceneFrame`) in Dungeon-/Ground-/Weltkarten-/
  Freundesareal-Frame; Kartenebenen (BG3, BG2 bei Zweischicht-Karten) werden vom Port aus
  `gDungeon`-Cels, `chunkMappings`/`tileMappings` bzw. den 64×64-Bildern im RAM gefüllt; BG0/BG1 (Text/HUD/Dunkelheit) am GBA-Rechteck,
  außerhalb leer bzw. Dunkel-Kachel 0x27A; Sprites über `Port_SpriteStaged`/`Port_OamSlot`
  mit erweitertem Culling (`gPortViewMargin*`); WIN/HDMA um (dx,dy) verschoben.
- Audio: m4a-Engine übersetzt, PCM-Block pro Bild, PSG emuliert (Musik hörbar im Host-Lauf).
- **Noch nicht auf echter Hardware/ROM getestet** – Mike muss die US-ROM nach
  `ux0:data/pmd/` legen und `log.txt` schicken.

## Offene Punkte (nächste Schritte)
1. Test mit Mikes ROM auf der Vita (0.1.5 ist der erste Stand, der auf dem Host mit der EU-ROM
   durchläuft): Startzeit (ROM-Scan ~3 s auf x86), Leistung (`[perf]`), Weitsicht in
   echten Dungeons (Dunkelheit in den Rändern: oben/unten nutzen Zeile 0/159 der WIN1-Tabelle;
   Rasteranzeige/Effekte auf BG2 bleiben auf das GBA-Rechteck beschränkt).
2. Weltkarte/Freundesareal-Karte sind in der Weitsicht (480×320-Bild), aber ungetestet
   (die Intro-Weltkarte ist eine Bodenkarte, Szene 2, 240 px breit sichtbar); Menüfenster prüfen.
   Deutscher Spieltext aus der EU-ROM (Stringtabellen der EU-Sprachen finden) nur nach Rücksprache.
3. Dungeon: `ShouldDisplayEntity` (±32 px) gilt auch für Logik – Monster/Items in den Rändern
   jenseits davon werden nicht gezeichnet; evtl. reine Render-Kopie einführen.
4. Leistung messen (Vita: 424×240 Software-PPU mit 3 Threads; Old-3DS-Profil aktiv).
5. **Schnellladen** sichert nur die RAM-Abschnitte (kein C-Stack): aus einer anderen Szene geladen
   (z. B. Dungeon-Zustand während des Intros, `PMD_QLOAD_AT=10`) stürzt es in `ground_weather.c`
   ab (`gUnknown_3001B74` NULL). Sicher nur innerhalb derselben Szene; Abhilfe: nach dem Laden den
   Hauptschleifen-Zustand neu aufsetzen (`GameLoop_Async` von oben) oder Laden nur im selben Modus erlauben.
6. Weitere sanfte Cheats (z. B. IQ-Multiplikator, Geld ×2) nur nach Rücksprache; Overlay/Karte
   wie beim TMC-Port.
