# Pokémon Rote Edition – nativer 3DS-Port

Dieser Ordner enthält den nativen Nintendo-3DS-Port von **pokered-de**. Das Spiel läuft nicht in einem Emulator. Der GB-Maschinencode wird beim Build **statisch nach C rekompiliert** und als ARM11-Code für den 3DS kompiliert. Nur die Game-Boy-Hardware (Grafik, Sound, Timer) wird in C nachgebildet.

| | |
|---|---|
| **Title ID** | `0004000000171300` (Rote Edition DE als VC-Titel) |
| **Spielstand** | `/sav.dat` im Spielstand-Archiv, 32 KiB, identisch zur VC-Version |
| **Oberer Bildschirm** | Spiel (Overworld, Kämpfe, Menüs), 1,5x weich/scharf oder 1:1 mit SGB-Rahmen |
| **Unterer Bildschirm** | Team, Karte, Optionen (Touch, L/R wechselt) |
| **Farbmodi** | DMG (Original), GBC (Boot-ROM-Palette), SGB (Spielpaletten + Rahmen) |

## Aufbau

```
port3ds/
├── core/      Laufzeitkern: CPU-Zustand, Speicher/MBC5, PPU, APU, Timer, SGB, Interpreter
├── recomp/    recomp.py (SM83 -> C) und seeds.txt (zur Laufzeit gefundene Einsprünge)
├── ui/        oberer + unterer Bildschirm (plattformunabhängig, RGB565)
├── 3ds/       libctru-Frontend, Makefile, app.rsf (CIA), Icon/Banner
├── host/      Linux-Testfrontend (Screenshots, Trace, WAV, Miss-Log)
├── tools/     gen_syms.py (Symbole -> syms.h), mkinput.py (Eingabeskripte)
└── tests/     Eingabe-Route für den Differenztest
```

### Wie die Rekompilierung funktioniert

1. `make red` baut die ROM wie gewohnt mit rgbds. Die ROM ist byte-identisch zur deutschen Roten Edition.
2. `recomp/recomp.py` liest `pokered.gbc`, `pokered.sym` und `pokered.map`. Ausgehend von allen Code-Labels der Decomp, den Interrupt-Vektoren und `seeds.txt` entdeckt es den Code rekursiv. Datensektionen (Grafiken, Texte, Karten) werden anhand der `.map` übersprungen.
3. Zusammenhängende Befehle werden zu einer C-Funktion. Register sind lokale Variablen, Sprünge innerhalb der Funktion sind `goto`. Bei `call`, `ret`, `jp hl` und Bankwechseln geht die Ausführung zurück an den Dispatcher.
4. Code, der nicht rekompiliert vorliegt (z. B. die OAM-DMA-Routine im HRAM), führt ein Interpreter aus. Das Ergebnis ist korrekt, nur etwas langsamer.

Eigene Änderungen am Hack (neue Funktionen, verschobene Daten) übernimmt der Port beim nächsten Build automatisch. Der untere Bildschirm liest Spielwerte und Grafiken ebenfalls über die Symbole (`build/syms.h`).

### Differenztest

`make difftest` spielt die Route in `tests/route_intro.txt` (Intro, Labor, Starter, Rivalenkampf) zweimal: einmal mit dem reinen Interpreter, einmal mit **präzise** rekompiliertem Code. Verglichen werden Frame für Frame WRAM, HRAM, VRAM und OAM. Beide Läufe müssen identisch sein.

## Bauen

Voraussetzungen: [devkitPro](https://devkitpro.org/wiki/Getting_Started) mit devkitARM und libctru, rgbds 1.0.1, Python 3. Für die CIA zusätzlich `makerom` ([Project_CTR](https://github.com/3DSGuy/Project_CTR)) und `bannertool`.

```sh
make red              # im Hauptordner: ROM bauen
cd port3ds
make                  # -> 3ds/pokered-de.3dsx
make cia              # -> pokered-de.cia (Title ID 0004000000171300)
make host             # Linux-Testbuild
make difftest         # Interpreter vs. Recompilat
```

Die GitHub Action `.github/workflows/3ds.yml` baut 3dsx und CIA bei jedem Push und lädt beide als Artefakt hoch.

## Hinweise

- **Offizielle VC-Version:** Ist sie installiert, wird sie durch die CIA ersetzt, weil beide dieselbe Title ID haben. Sichere den Spielstand vorher mit Checkpoint oder PKSM.
- **PKSM:** In den PKSM-Einstellungen unter Title IDs muss für die Rote Edition `0x0004000000171300` eingetragen sein. Standardmäßig steht dort die englische `...171000`. PKSM findet `/sav.dat` dann wie bei der VC-Version.
- **Ton:** Ton braucht `sdmc:/3ds/dspfirm.cdc`, das du mit DSP1 erzeugst. Ohne die Datei läuft das Spiel stumm; auf dem unteren Bildschirm unter Optionen steht dann „Ton: aus“.
- **Fehlersuche:** Der Port schreibt den Startablauf und Fehlercodes nach `sdmc:/3ds/pokered-de/log.txt`. Abgesicherter Start: **L** beim Starten gedrückt halten startet ohne Ton, **R** ignoriert die gespeicherten Einstellungen. Die Symboltabelle `pokered-de-symbole.txt` im CI-Artefakt ordnet Absturzadressen Funktionen zu.
- **3dsx:** Gestartet aus dem Homebrew Launcher liegt der Spielstand unter `sdmc:/3ds/pokered-de/sav.dat`.
- **Beenden:** HOME-Taste oder START+SELECT+L+R.

## Kampf-Menüs auf dem Touchscreen

Im Kampf übernimmt der untere Bildschirm die Menüs des Spiels: Hauptmenü (KAMPF, POKéMON, ITEM, FLUCHT), Attacken mit Typ und AP, Beutel mit Bildlauf und das Team mit Untermenü (WECHSELN, WERTE, ZURÜCK). Die Darstellung folgt dem Spiel (Rahmen, Schrift, Cursor, Team-Icons aus der ROM).

Das Spiel läuft unverändert weiter. Die UI erkennt am Game-Boy-Stapel, in welcher Menüfunktion das Spiel gerade wartet (`RANGES` in `tools/gen_syms.py`), liest den Cursor aus dem WRAM und zeichnet das Menü. Ein Tipp wird in Steuerkreuz- und A-Drücke übersetzt (`ui_virtual_buttons()`), die wie echte Tasten an das Spiel gehen. Steuerkreuz + A funktionieren deshalb genau wie vorher, und der Cursor bleibt auf beiden Bildschirmen gleich. Der Knopf ZURÜCK sendet B. Nicht übernommen werden Safari-Zone, Lehrkampf, Link-Kampf und die Attacken-Menüs von Mimikry und Attackenvergessen. Dort bleibt der Bildschirm bei Team/Karte/Optionen.

Test im Host-Build (zeichnet nur bei Änderung, wie das 3DS-Frontend):

```sh
./build/pokered_host ../pokered.gbc -i eingabe.txt -f 21000 -b 25 -T 20640
```

`-b N` schreibt alle N Frames den unteren Bildschirm nach `shots/bNNNNN.ppm`, `-T N` legt im Frame N ein Beispiel-Team und einen gefüllten Beutel an. Im Eingabeskript simuliert eine Zeile `FRAME tX,Y` einen Touch, zum Beispiel `20730 t80,160` für ITEM im Hauptmenü.

## Steuerung

| 3DS | Spiel |
|---|---|
| A / B (auch Y) | A / B |
| START / SELECT | START / SELECT |
| Steuerkreuz / Schiebepad | Steuerkreuz |
| L / R | Seite unten wechseln (Team / Karte / Optionen) |
| Touch | Seite wählen, Optionen ändern; im Kampf Menüeinträge antippen (siehe unten) |
