# Pokémon Mystery Dungeon: Red Rescue Team für die PS Vita

## Installation

1. `MysteryDungeonRed_Vita_<version>.vpk` mit VitaShell installieren.
2. Deine ROM von *Pokémon Mystery Dungeon: Red Rescue Team* (USA) nach
   `ux0:data/pmd/` kopieren. Der Dateiname ist egal, Endung `.gba`.
   Erkannt wird sie an der Prüfsumme
   (SHA-1 `9f4cfc5b5f4859d17169a485462e977c7aac2b89`, 32 MiB).
3. Starten. Beim ersten Start durchsucht der Port die ROM einmal nach den
   Spieldaten (einige Sekunden) und merkt sich die Fundstellen in
   `romindex.txt`; danach startet er sofort.

Im Paket sind keine Spieldaten. Grafik, Karten, Texte und Musik kommen beim
Start aus deiner ROM.

## Steuerung

| Vita | Spiel |
|------|-------|
| Kreuz | A (bestätigen, angreifen) |
| Kreis / Viereck | B (abbrechen, rennen) |
| L / R | L / R |
| Steuerkreuz / linker Stick | Bewegen |
| START / SELECT | Menü / Karte wie auf dem GBA |
| Dreieck halten | Schnellvorlauf (wenn im Port-Menü eingeschaltet) |
| START + SELECT | Port-Menü |
| Rücktouch halten (wenn an) | links: schnellspeichern, rechts: schnellladen |

## Menü (START + SELECT)

Ansicht (GBA, Breitbild, Weitsicht), Bilddarstellung (Weich, Scharf,
Pixelgenau), Bildrate 60/30, Schnellvorlauf, Rücktouch, Schnellspeichern /
-laden und Cheats (KP des Anführers, Bauch, Geld). Die Einstellungen landen
in `settings.ini`. Die Versionsnummer steht oben im Menü und im LiveArea.

Die **Weitsicht** (400×240) zeigt in Dungeons und in den Orten (Platz, Basis,
Zwischensequenzen) mehr von der Karte; Texte, HUD und Menüs bleiben an ihrem
Platz. Titelbild, Karten und Vollbildmenüs bleiben bei 240×160. Karten, die
kleiner als das Fenster sind, bekommen einen schmaleren Rahmen mit gleichem
Zoom.

Schnellspeichern geht nur im laufenden Spiel (Dungeon oder Ort). Eine
Schnellspeicherung gehört zu genau einer Version des Ports; nach einem
Update ist sie ungültig. Die normalen Spielstände bleiben davon unberührt.

## Dateien in `ux0:data/pmd/`

| Datei | Inhalt |
|-------|--------|
| `*.gba` | deine ROM |
| `pmd_red.sav` | Spielstand (128 KiB Flash, mit GBA-Emulatoren kompatibel) |
| `settings.ini` | Einstellungen |
| `quicksave.bin` | Schnellspeicherung |
| `romindex.txt` | Fundstellen der Spieldaten in der ROM (wird automatisch erzeugt) |
| `log.txt`, `log_prev.txt` | Protokoll des letzten / vorletzten Starts |

Bei Problemen bitte `log.txt` mitschicken. Die `[perf]`-Zeilen zeigen die
Rechenzeit pro Bild und wie viele Bilder ausgelassen wurden (`catchup`).

## Host-Tests (Linux)

`build/port-host/pmd_host rom.gba <frames> <ausgabeordner> [skript]` –
Skriptzeilen `<frame> <tastenmaske-hex>` (A=1, B=2, Select=4, Start=8,
rechts=10, links=20, oben=40, unten=80, R=100, L=200); die Tasten bleiben bis
zur nächsten Zeile gedrückt. Umgebungsvariablen: `PMD_SHOT=f1,f2` (PPM-Bilder),
`PMD_SHOT_EVERY=n`, `PMD_VIEW=0/1/2`, `PMD_WAV=1`, `PMD_QSAVE_AT`/`PMD_QLOAD_AT`,
`PMD_CHEATS=1`, `PMD_30FPS=1`, `PMD_TIMING=1`, `PMD_VIEW_DUMP=frame`,
`PMD_AUDIO_DEBUG=1`. `settings.ini`, `quicksave.bin` und `romindex.txt`
liegen im Ausgabeordner.
