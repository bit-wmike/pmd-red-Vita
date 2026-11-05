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
- Agenten parallel: eigene Build-Verzeichnisse (`BUILD=build/agentX-<target>`), disjunkte Dateien; wenn
  einer einen Header ändert, müssen alle anderen ihre Build-Verzeichnisse löschen.
- **Vita-GCC 15 ist strenger** → vor jeder Version `TARGET=vita` bauen. Der Präprozessor-Schritt
  braucht `-std=gnu11` (sonst `nullptr`-Fehler aus `stddef.h`).

## Testen (Host)
`build/port-host/pmd_host rom.gba <frames> <ausgabeordner> [skript]`, Skriptzeilen
`<frame> <tastenmaske-hex>` (A=1, B=2, Select=4, Start=8, rechts=10, links=20, oben=40,
unten=80, R=100, L=200; Tasten bleiben bis zur nächsten Zeile gedrückt).
Schalter: `PMD_SHOT=f1,f2` / `PMD_SHOT_EVERY=n` (PPM), `PMD_VIEW=0/1/2`, `PMD_WAV=1`,
`PMD_QSAVE_AT`/`PMD_QLOAD_AT`, `PMD_CHEATS=1`, `PMD_30FPS=1`, `PMD_TIMING=1`,
`PMD_VIEW_DUMP=frame` (OAM/Ansicht), `PMD_AUDIO_DEBUG=1`.
- `platform/host/scripts/intro_to_dungeon.txt`: Neues Spiel bis in Kleinhain B1F (Partner Bisasam, Dungeon
  ab Bild ~8975, 14000 Bilder; B gehalten überspringt Text). Pflichtlauf vor jeder Version (Dungeon-Start).
- Ohne echte ROM: `python3 tools/port/fake_rom.py build/fake.gba` (nach einem Build) – das
  Spiel läuft damit bis in die Spielwelt (Intro-Skript, Basis), Musik ab ~Frame 968;
  `baserom.gba`-Teile (u. a. PCM-Samples) sind darin Nullen.
- Prüfregeln vor jeder Version: (1) Host x86 und armhost (qemu) liefern **byteidentische
  PPMs** (`cmp`); (2) Renderer-Änderungen Bild für Bild gegen die alte Version vergleichen;
  (3) Vita-Build ohne Fehler.

## Stand (0.1.15, 05.11.2026)
- 0.1.12 (Mikes `run4`, 0.1.11: Absturz vor dem ersten Dungeon, Titel/Gesundheitshinweis englisch bzw.
  „fehlerhaft“, Tastatur-Müll bei der Namensgebung, Traumtext nicht zentriert): (1) **Absturz**: `lvmp001`
  (Levelkurve, AT-gepackt, 616-Byte-Block mit SIRO-Kopf) wird aus der EU-ROM genommen; `Port_ChooseFile`
  zählte den SIRO-Zeiger des Kopfes als „Zeiger im Inhalt“ und ließ `Port_RelocateRomFile` die gepackten
  Daten nach Aussehen „übersetzen“ (5 Wörter zerstört → Dekompression läuft Amok). Jetzt zählt
  `Port_AssetPointersFrom(p)` nur Löcher ≤ 64 Byte ab `p` (große Löcher = baserom-Daten). Auf dem Host nicht
  bis zum Dungeon nachgespielt (Skript kommt nicht am Smettbo-Gespräch vorbei) – Mike testet.
  (2) **Tastatur**: `#C4OVR#R` wurde per Tabellenzuordnung zu „$m0 legt das Item $i0 an.“ (Parallel-Lauf ohne
  erkennbare Sprache, Position genommen). `Translate` verwirft jetzt Kandidaten, deren Platzhalterfolge
  (`$`/`%`) nicht zur Vorlage passt (`Placeholders`); `PMD_TEXT_DEBUG=2` protokolliert jede Übersetzung.
  (3) **Sprachvarianten der Bild-Karten**: Die EU-ROM hat `S02_GER/S02_GERc/S02_GERm`, `S05_…`, `S06_…` (Titel
  „TEAM ROT“, Gesundheitshinweis, S06) je Sprache (`_GER/_FRE/_ITA/_SPA`); `Port_VariantFile`
  (file_system.c) nimmt sie statt der US-Datei, Tilesets über `Port_RomTilesetFile` (entpackt, gecacht).
  (4) **Titel**: S0x-Karten (`PictureMapName`, auch Varianten) werden jetzt als Karte mit Breite der Karte und
  Höhe des GBA-Fensters gezeigt (`sPictureMap`; `FillGround` blendet Zeilen außerhalb der Kamera-160 aus) –
  vorher nur das VRAM-Fenster mit einer schwarzen Kerbe unten in den Randspalten. Ränder = Hintergrundfarbe
  (Himmel), oben/unten schwarz.
  (5) **Traumtext** („Wo bin ich?“) steht links: die Szene ist die Kleinhain-Karte `D01P01` (432×360) mit
  schwarzer Palette, Kamera in der Ecke (−1,0) → GBA-Fenster links im Bild (Fenster folgt der Kamera und stoppt
  am Kartenrand), der Text ist im GBA-Fenster zentriert. Nicht behoben; Alternative wäre eine fest zentrierte
  UI-Ebene (BG0 mit eigenem dx) – Rücksprache mit Mike. `UniformMap` (einfarbige Karten → Bild mittig) ist
  drin, greift hier aber nicht (Karte hat echte Kacheln).
  (6) `vita.sh fetch` holt jetzt auch `pmd_red.sav`.
- 0.1.15 (Mikes `run5`, 0.1.14: Freeze beim ersten Dungeon weiterhin, Traumtext/Dialogboxen links, Weltkarte
  im Intro nur GBA-groß): (1) **Freeze gelöst und auf dem Host reproduziert** (Skript
  `scratchpad/dungeon_b.txt`: Partner **Bisasam** → `lvmp001`; 0.1.14-Host stürzt mit Signal 11 beim
  Dungeon-Start, neu läuft durch). Ursache: `Port_ChooseFile` prüfte „ist SIRO?“ an der Port-Kopie, die bei
  einem nicht gefundenen Block **Nullen** enthält → Schwelle 0 → ganze gepackte Datei nach Aussehen
  „zeigerübersetzt“ (5 Wörter). Jetzt: ROM-Datei mit SIRO + AT4PX/AT4PN/ATUPX/PKDPX → nie übersetzen
  (`Port_IsCompressedMagic`); außerdem lässt `Relocate` die 336 Pseudo-Relocations in 40 gepackten
  system_sbin-Dateien aus (`InCompressedContent`; betrifft die US-ROM, bei der die Blöcke gefunden werden).
  Der Host-Skriptleser liest jetzt 8192 Zeilen (vorher 1024 – längere Skripte drückten ab Zeile 1024 nichts
  mehr; „Hänger“ in Dialogen waren nur wartende Textboxen). Skripttrick: B dauerhaft halten lässt alle
  Textboxen durchlaufen, A-Flanken alle 60 Bilder beantworten Menüs; Tastatur: Start springt auf END.
  (2) **UI-Ebenen mittig**: BG0/BG1 und Bildschirm-UI-Sprites (alles über `AddSprite`, außer Schatten,
  Fallen, Boden-Marker, die `gPortSpriteWorld` setzen) liegen immer am GBA-Rechteck in der Bildmitte
  (`sUiDx/sUiDy`, `sSlotUi[]`); die Welt (BG2/3, `AddAxSprite`-Sprites, WIN/HDMA) folgt der Kamera wie
  bisher. Traumtexte, Dialogboxen und Portraits sind damit mittig, auch wenn die Kamera am Kartenrand steht.
  (3) Dungeon-Sichtweite: `ShouldDisplayEntity` und `trap.c` nutzen `gPortViewMargin*` (Monster/Items/Fallen
  im ganzen Weitsicht-Bereich; wirkt auch auf Animationslogik sichtbarer Entities).
  (4) Intro-Weltkarte = Bodenkarte **S03** (288×312, Kamera schwenkt vertikal): mit der Bildregel 288 px
  breit. Menühintergrund (schwarz mit Funkeln) ist GBA-echt. `PMD_VIEW_LOG=1` (Host) loggt Ansichtswechsel.
  Offen: Dungeon-Titel „Tiny Woods B1F“ englisch (Banner).
- 0.1.14 (zwei Agenten): (1) **Startzeit-Caches**: `romindex.txt` hat Kopf `pmdidx 2 <TableHash> <ROM-SHA-1>`
  (FNV-64 über die Blocktabelle statt Versionsnummer → neue Version ohne Datenänderung nutzt den Cache);
  nicht gefundene Blöcke stehen als `name -` drin (`absent`, sonst 5 Scan-Pässe pro Start für 765 Blöcke).
  `textindex.bin` (port_text.c, ~10,5 MiB, Header `TIDX`, Format 1, SHA-1; sIndex/sSites/Pools/Regionen in
  einem Block `sIndexBlob`, direkt eingelesen) ersetzt den Indexbau; **`TEXT_INDEX_FORMAT` erhöhen**, wenn
  BuildIndex/BuildPools/BuildRegions/PreferEnglishStrings oder Tabellengrößen geändert werden. x86 warm:
  Assets 181 ms, Textindex 5 ms (vorher 2300/400). Host-Pfade: `<ausgabeordner>/romindex.txt`, Vita
  `ux0:data/pmd/`. `Port_RomSha1()` (port.h). Bekannt: Host-Zeiger, die `Port_RelocateRomFile` in `gHostRom`
  schreibt, landen als „Strings“ im Index (≈340 Einträge, heap-abhängig, wirkungslos).
  (2) **Rahmen um das Spielbild** (main_vita.c, Menü „Rahmen um das Spielbild“, ini `frame` 0/1/2, Standard 1
  = Artwork): LiveArea-bg abgedunkelt (Tint 100/255) hinter dem Spielbild bzw. 9-Slice-Bezel
  (`platform/vita/assets/bezel.png` aus `tools/make_bezel.py`); vita2d der VitaSDK hat keinen PNG-Lader →
  `tools/png2raw.py` macht beim VPK-Bau `assets/bg.raw` (480×272) und `assets/bezel.raw` (Kopf `PRAW`,w,h).
  Nur sichtbar, wenn das Spielbild Ränder hat (Pixelgenau, GBA-Ansicht). Nicht auf der Vita getestet.
  `struct PortConfig` hat ein neues Feld `frame` → alle Build-Verzeichnisse frisch gebaut.
- 0.1.13: **LiveArea-Hintergrund grau** (Mikes Screenshot): `stamp_version.py` schrieb bg/startup als
  32-Bit-RGBA-PNG; die Vita zeigt in der LiveArea nur 8-Bit-Paletten-PNGs (sonst graue Seite). Jetzt
  Quantisierung auf 256 Farben beim Stempeln. Regel: alle sce_sys-PNGs müssen Modus `P` haben.
- 0.1.11 (Mikes `run3`-Screenshots): (1) **Sprite-Fragment** (Glumandas Schwanzflamme links unten):
  das Spiel parkt unbenutzte OAM-Einträge unter Zeile 160; der Renderer rechnete sie wie
  Port-platzierte Sprites um (Basis aus altem Slot) → sichtbar im Rand. Jetzt merkt
  `Port_OamSlot` das Bild der Platzierung (`sSlotFrame`), `virtuappu_mode1_obj_staged[]` sagt dem
  Renderer, welche Slots der Port gesetzt hat; alle anderen folgen GBA-Regeln (y ≥ 160 und
  nicht umgebrochen = unsichtbar; sonst 8/9-Bit-Koordinaten + dx/dy). (2) **Wolken halbiert**: die
  Wetterebene ist eine zweite `GroundBg` (`gUnknown_3001B78`, ground_weather.c) mit eigener
  Kamera, die das Spiel nur für den GBA-Ausschnitt in BG2 zeichnet; `port_view.c` füllt sie jetzt
  wie die Hauptkarte aus ihren Chunk-/Kachel-Tabellen über das ganze Bild (`bg2Done`).
  (3) VPKs liegen zusätzlich im Daten-Repo unter `builds/`; `./vita.sh install` holt die neueste
  per `git pull` und lädt sie auf die Vita (Mike arbeitet vom iPhone per SSH auf dem Pi).
  Header `port/ppu/include/cpu/mode1.h` geändert → alle Build-Verzeichnisse frisch gebaut.
- 0.1.10 (Mikes `run3`: Absturz beim ersten Brief, Gesundheitswarnung „springt“, englische
  Dungeon-Menübefehle, Sprite-Fragment, Wolken am Briefkasten):
  (1) **Absturz** in `ReadAnimatedColorData` (NULL-Zeiger): `gUnknown_839B7D4` (titlemenu_sbin)
  ist eine Zeigertabelle, deren 2. Hälfte im Decomp als `.incbin baserom.gba` steht → der Block
  besteht nur aus Löchern, `keysel` machte daraus einen Konstantblock (Füllwert 0) → Nullzeiger.
  Jetzt: `HoleOnly`-Blöcke werden nicht gefüllt, sondern nach den Scans neben ihren gefundenen
  Nachbarn (Elternblock oder direkt angrenzendes Geschwister, Zustand 6) platziert
  (`PlaceByNeighbours`, 70 Blöcke); kleine Löcher (≤ 64 Byte) gelten als Zeigertabellen und werden
  nach dem Rohkopieren in Host-Zeiger auf `gHostRom` übersetzt (`TranslateSmallHoles`, auch in
  `Verify`/`TryPartial`). Der Quiz-Fragenzufall ändert sich dadurch (Fragetabellen waren Nullen).
  (2) **Gesundheitswarnung** ist die Bodenkarte S05 (Inhalt 264×240) → wie S02 als Bild behandelt.
  (3) Menübefehle im Dungeon („Get“ …): Struct-Tabellen mit Stride 8/12/16 (`sStride`), Sprache
  einer Tabelle notfalls aus der direkt folgenden Code-Datenregion (`LangNearRegion`, ≤ 4 KiB);
  englische Pools/Regionen (+ 4 KiB davor) überschreiben gleichlautende Fremdwörter im Index
  (`PreferEnglishStrings`: FR „Place“ verdeckte EN „Place“).
  (4) Offen: Sprite-Fragment (Glumandas Schwanzflamme) links unten im Haus/am Briefkasten –
  vermutlich ein OAM-Eintrag ohne Port-Versatz; braucht OAM-Dump der Szene. Wolken am Briefkasten
  „halbiert“ (BG2-Wetterebene in der Weitsicht) – Screenshot fehlt noch. Vita-Log `catchup 0`,
  14–17 ms/Bild. Mike wünscht Rahmen in der Pixelgenau-Ansicht: Standard Artwork-Hintergrund
  (LiveArea-bg abgedunkelt), umschaltbar auf echten Bezel mit Randgrafik; Startzeit (39 s beim
  ersten Start) durch vollständigen Cache (auch Textindex) verkürzen statt ROM in die VPK.
- 0.1.9 (Mikes `run2`: Absturz beim Aufheben einer Sinelbeere, fehlende NPC-Gesichter, dunkle
  Ränder im hellen Dungeon, Titelbild-Streifen, englische Dungeon-Meldungen auf der Vita):
  (1) **EU-Itemtabelle** (`itempara`) hat keine Namens-/Beschreibungszeiger (die EU sucht Namen
  per ID); der US-Code las dort 0 → Absturz. `Port_FillPointerFields` (host_assets.c) trägt die
  Zeigerfelder aus der Port-Kopie (Relocations) in die ROM-Datei ein, in `Port_ChooseFile` über
  den SIRO-Inhalt (US-Datei hat die Beschreibungen zwischen Header und Tabelle).
  (2) **baserom.gba-Teile** (Portraits `kao`, PCM-Samples) waren Löcher und wurden beim Laden
  genullt – `Verify`/`TryPartial` kopieren jetzt die rohen ROM-Bytes (Hash weiter über den
  Zwischenpuffer mit genullten Löchern; Zeigerlöcher patcht `Relocate`). Gesichter sind da.
  (3) **Dunkle Ränder**: BG1 außerhalb des GBA-Rechtecks bekam immer die Dunkel-Kachel 0x27A;
  jetzt nur, wenn die BG1-Tilemap des Spiels in den Ecken selbst 0x27A hält (`DarknessActive`).
  (4) **Titelbild**: der Titel ist die Bodenkarte **S02** (288 px breit, mit Restzeilen unter dem
  Meer); die Weitsicht zeigte diese Zeilen als Streifen unter dem Bild. `GroundInfo` behandelt
  `S02c` als Bild ohne Karte; `Port_ViewBlankRows` schwärzt die Zeilen außerhalb der 160 GBA-
  Zeilen (`RenderFrame`). Die Gesundheitswarnung ist ein weißes Vollbild (kein Fehler erkennbar).
  Debug: `PMD_DARK_DEBUG=1` loggt alle 300 Bilder Szene, Inhalt, Fenster-/BG1-/DMA-Register und bpc-Name.
  (5) Versionsnummer auf LiveArea-Bildern: `stamp_version.py` beim VPK-Bau (Makefile.port).
  (6) `settings.ini`-Schlüssel `text_debug=1` protokolliert auf der Vita unübersetzte Texte.
  **Lehre:** Nach Änderungen an `port/*.h` immer `rm -rf build/port-*` – sonst Struct-Versatz
  (`gPortConfig.language` landete in `textDebug`, Text wieder englisch).
- 0.1.8: **Quiz-Absturz gelöst** (Mikes `run1` im Daten-Repo: Dump + crumbs). Ursache: acht
  Textfunktionen in `src/text_4.c` stehen im Decomp mit `IWRAM_INIT` (Sektion `iwram_init`, auf
  dem GBA nach IWRAM kopiert) – im Vita-ELF lagen sie damit im **Datensegment** (RWE), das die
  Vita nicht ausführt; der Sprung über `gIwramTextFunc3` → `sub_82729A4` in
  `DrawDialogueBoxString_Async` (erster Textbox-Scroll am Quiz-Ende) stürzte ab. Host und qemu
  führen Daten aus, darum dort unauffällig. Fix: `IWRAM_FUNC` (unter PORT leer); Datensegment ist
  jetzt RW, `nm` zeigt keine T-Symbole mehr ab `0x824f0000` (Prüfregel für künftige Versionen).
  Dump-Auswertung: psp2dmp ist gzip → `gunzip`, dann `/home/user/dumps/parse_dump.py`; Modul-
  Segment 1 = ELF `0x81000000`, Segment 2 (Daten) = ELF `0x824f0000` (aus `readelf -l`).
  Leistung laut `log.txt` (424×240, Quiz/Intro): frame work 17–18 ms, render ~14 ms, `catchup`
  steigt → unter 60 FPS; Punkt 4 der offenen Liste wird wichtig (30-FPS-Modus existiert).
- 0.1.7: Mikes Test von 0.1.6: **Absturz am Ende des Quiz** (bei der letzten Frage bzw. bei „du
  scheinst der … Typ zu sein“); der psp2dmp bleibt bei 10 % hängen. Auf dem Host läuft dieselbe
  Stelle durch (auch unter qemu-arm). Deshalb: **Breadcrumbs** – `Port_Crumb()` (port_game.c)
  sammelt die letzten 24 Aktionen (geöffnete Dateien, Textabfragen, Szenenwechsel); die Vita
  schreibt sie alle 30 Bilder nach `ux0:data/pmd/crumbs.txt` (`WriteCrumbs` in main_vita.c).
  Nach einem Absturz zeigt die Datei, was zuletzt passierte. Verdächtige (ungeprüft): Vita-spezifisch
  sind nur Speicher (Heap 160 MiB: ROM 32 + Textindex ~11 MiB), Ausrichtung, Thread-Stack.
  **Daten-Workflow:** `platform/vita/tools/vita.sh` (läuft auf Mikes Pi im Daten-Repo
  `pmd-vita-data`): `./vita.sh fetch runN` holt per FTP (VitaShell) log.txt, log_prev.txt,
  crumbs.txt, settings.ini, quicksave.bin, alle `ux0:data/*.psp2dmp` und alle Screenshots,
  löscht Screenshots/Dumps auf der Vita, committet und pusht; `./vita.sh vpk <datei>` lädt eine
  VPK nach `ux0:data/pmd/`. Konfiguration `vita.conf` (`VITA=<IP>`). Die Läufe liegen dann unter
  `runs/<name>/` im Daten-Repo (per `add_repo` in der Cloud-Sitzung lesbar).
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
3. ~~Dungeon: `ShouldDisplayEntity` (±32 px)~~ seit 0.1.15 um die Weitsicht-Ränder erweitert (auch für Logik).
4. Leistung: Mikes Log (0.1.7) zeigt in der Weitsicht 17–18 ms Arbeit pro Bild (Render ~14 ms),
   also knapp unter 60 FPS mit steigendem `catchup`. Renderer-Profil wie beim TMC-Port (Kachelpfade,
   Compositor) oder standardmäßig 30 FPS für die Weitsicht erwägen.
5. **Schnellladen** sichert nur die RAM-Abschnitte (kein C-Stack): aus einer anderen Szene geladen
   (z. B. Dungeon-Zustand während des Intros, `PMD_QLOAD_AT=10`) stürzt es in `ground_weather.c`
   ab (`gUnknown_3001B74` NULL). Sicher nur innerhalb derselben Szene; Abhilfe: nach dem Laden den
   Hauptschleifen-Zustand neu aufsetzen (`GameLoop_Async` von oben) oder Laden nur im selben Modus erlauben.
6. Weitere sanfte Cheats (z. B. IQ-Multiplikator, Geld ×2) nur nach Rücksprache; Overlay/Karte
   wie beim TMC-Port.
