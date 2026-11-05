# Pokémon Mystery Dungeon: Red Rescue Team für die PS Vita

## Installation

1. `MysteryDungeonRed_Vita_<version>.vpk` mit VitaShell installieren.
2. Deine ROM von *Pokémon Mystery Dungeon: Red Rescue Team* nach
   `ux0:data/pmd/` kopieren. Der Dateiname ist egal, Endung `.gba`, 32 MiB.
   Der Port ist aus der US-Version gebaut (SHA-1
   `9f4cfc5b5f4859d17169a485462e977c7aac2b89`); die Europa-ROM (B24P) wird
   ebenfalls unterstützt. Ihre Datenblöcke werden über den Inhalt gefunden,
   abweichende Blöcke (Texte, Schriften) teilweise übernommen, die gepackten
   Boden-Tilesets der EU-ROM werden beim Start entpackt. Mit der EU-ROM fragt
   der Port beim ersten Start einmal nach der **Sprache** (Deutsch, Englisch,
   Französisch, Italienisch, Spanisch); später lässt sie sich nur noch im
   Port-Menü ändern, damit die Frage bei vorhandenem Spielstand nicht stört.
   Titelbild („TEAM ROT“), Gesundheitshinweis und S06 kommen in der gewählten Sprache
   aus der EU-ROM (eigene Karten `S02_GER`, `S05_GER` …).
   Der Port nimmt die Texte des US-Spielcodes und sucht die Übersetzung in der
   ROM; einzelne Texte können englisch bleiben oder (bei geteilten
   Standardtexten) eine Nachbarbeschreibung zeigen. Ein Hinweis beim
   Start und `log.txt` zeigen, wie viele Blöcke nur teilweise oder gar nicht
   passten. Der Spieltext ist in beiden Fällen der englische Text der
   Dekompilierung. Fehlende Dateien stehen in `log.txt` als
   `file: not found in the game data: <name>`.
3. Starten. Beim ersten Start durchsucht der Port die ROM einmal nach den
   Spieldaten (einige Sekunden) und merkt sich die Fundstellen in
   `romindex.txt`; danach startet er sofort.

Im Paket sind keine Spieldaten. Grafik, Karten, Texte und Musik kommen beim
Start aus deiner ROM.

## Steuerung

| Vita | Spiel |
|------|-------|
| Kreuz | A (bestätigen, angreifen) |
| Kreis | B (abbrechen; B + Richtung = rennen) |
| Viereck | Gesetzten Gegenstand werfen (L+R im Spiel; im Menü auf B umstellbar) |
| Dreieck | Gesetzte Attacke einsetzen (L+A im Spiel; im Menü auf B umstellbar) |
| L / R | L / R (R + Richtung = diagonal gehen / umdrehen) |
| Steuerkreuz / linker Stick | Bewegen |
| Rechter Stick | Diagonal gehen (R + Richtung), abschaltbar |
| START / SELECT | wie auf dem GBA (SELECT: Etagenkarte, START: Drehmodus) |
| Touchscreen halten | Schnellvorlauf (abschaltbar) |
| START + SELECT | Port-Menü |
| Rücktouch | bewusst unbelegt (zu leicht versehentlich berührt) |

## Menü (START + SELECT)

Das Menü ist in Abschnitte geteilt und scrollt. Alle Komfortfunktionen und
Cheats sind einzeln ein- und ausschaltbar; die Einstellungen landen in
`settings.ini`. Die Versionsnummer steht oben im Menü und im LiveArea.

- **Bild:** Bilddarstellung (Weich, Scharf, Pixelgenau), Rahmen um das
  Spielbild (siehe unten), Bildrate 60/30, Sprache (nur EU-ROM). Die Ansicht
  ist immer die Weitsicht (424×240).
- **Steuerung:** Viereck, Dreieck, rechter Stick, Schnellvorlauf per Touchscreen.
- **Komfort:** Text sofort komplett anzeigen; A halten blättert Text weiter;
  Dungeon-Tempo ×1,5/×2 (Bewegungen und Gegnerzüge, Musik unverändert);
  schneller laufen in Basis und Stadt; Schnellspeichern bei jedem
  Etagenwechsel; HP-Balken über Gegnern (aus, nur Bosse, alle); Uhrzeit und
  Spielzeit im Rand.
- **Schnellspeicher:** Schnellspeichern / Schnellladen (nur in derselben Szene sicher).
- **Sanfte Cheats:** Erfahrung ×1–4, Geld ×1–4, IQ ×1–4, Rekrutierungschance
  +20 %/+50 %, Treppe auf der Karte markiert, kein Hunger, und die
  Team-Werte Angriff, Verteidigung, Spezial-Angriff, Spezial-Verteidigung,
  Initiative je −30 % bis +30 % (nur für das eigene Team).

Die **Weitsicht** (424×240) ist immer an: die Bildgröße und der Zoom ändern
sich nie. In Dungeons, in den Orten (Platz, Basis, Zwischensequenzen), auf der
Weltkarte und der Freundesareal-Karte zeigen die Ränder mehr von der Karte;
Texte, HUD und Menüs bleiben an ihrem Platz. Auf allen anderen Bildschirmen
(Titel, Vollbildmenüs, feste Bilder) sitzt das GBA-Bild mittig, die Ränder
bleiben schwarz.

**Rahmen um das Spielbild:** Füllt das Spielbild den Bildschirm nicht ganz
(Bilddarstellung „Pixelgenau“), zeigt der Port um das Bild
herum statt Schwarz wahlweise das **Artwork** (das LiveArea-Hintergrundbild
der VPK, abgedunkelt; Standard), einen **Bezel** (dunkle Rahmengrafik mit
Lichtkante um das Bild, `settings.ini`-Schlüssel `frame`: 0 = aus, 1 = Artwork,
2 = Bezel) oder nichts. Das Spielbild selbst bleibt unverändert; füllt es den
Bildschirm, ist vom Rahmen nichts zu sehen.

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

## Testdaten mit einem Befehl holen (Pi)

`platform/vita/tools/vita.sh` liegt auch im Daten-Repo. Einmalig: `sudo apt install lftp`,
`vita.conf` mit `VITA=<IP der Vita>` anlegen (VitaShell → SELECT startet den FTP-Server und
zeigt die Adresse). Danach nach jedem Test:

```sh
cd ~/pmd-vita-data && git pull && ./vita.sh fetch run3
```

holt `log.txt`, `log_prev.txt`, `crumbs.txt`, `settings.ini`, die Schnellspeicherung, alle
Core-Dumps und alle Screenshots nach `runs/run3/`, löscht Screenshots und Dumps auf der Vita
und pusht alles. `./vita.sh vpk <datei.vpk>` kopiert eine neue VPK nach `ux0:data/pmd/`
(dann in VitaShell installieren). `crumbs.txt` enthält die letzten Aktionen des Spiels vor
einem Absturz.
