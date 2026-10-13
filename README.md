# esp32-kids-adhs

Ein ruhiger Begleiter für Kinder auf dem **Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)**. Er soll helfen, mit „Wut im Bauch“ umzugehen: Gefühle benennen, Wut wegfüttern, gemeinsam atmen. Dazu kommen ein ruhiges Geschicklichkeitsspiel und ein Pferde-Album zum Sammeln. Es ist eine Unterstützung im Alltag, keine Therapie.

![Kinderversion im Simulator](doc/kinder-ablauf.png)

## Kacheln

| Kachel | Was passiert |
|---|---|
| Wie geht's dir? (Start) | fünf Gesichter (wütend, kribbelig, traurig, müde, froh). Das Gerät schickt zum passenden Werkzeug und fragt danach noch einmal nach |
| Grummel | schnell aufs Display tippen, auf den Tisch klopfen oder schütteln: Wutfunken fliegen in Grummel, er wird satt, rülpst, verwandelt die Wut in Blasen und atmet mit („Riech an der Blume“, „Puste die Kerze aus“) |
| Atmen | ein Ballon atmet mit: 4 s ein, 5 s aus, 4 Runden |
| Kugel ins Loch | Kugeln ins Loch rollen, per Finger als Joystick oder per Neigen. Ohne Zeitdruck, nach 4 Runden kommt eine Pause |
| Pferde-Album | 24 Pferderassen als Sammelkarten mit Foto, Typ, Herkunft, Größe, Farben, Wesen und „Wusstest du?“. Alle 3 Sterne aus den Übungen gibt es eine neue Karte, nie doppelt |

Sterne gibt es fürs Benutzen der Werkzeuge, nicht fürs „Bravsein“. Gestaltungsregeln: ruhige Bilder, nichts blinkt, kurze Runden ohne Zeitdruck, keine Ranglisten, freundliche Rückmeldungen, Ton startet aus.

## Bedienung

| Geste | Wirkung |
|---|---|
| Tippen, wenn das Display schwarz ist | aufwecken (das Gerät startet schwarz und geht nach 3 min Ruhe wieder aus) |
| Gesicht antippen | Gefühl wählen |
| nach links/rechts wischen | Kachel wechseln (im Ballspiel nur vom Rand aus) |
| 3 s lang drücken oder BOOT | Einstellungen für Eltern (Ton, Klopf-Empfindlichkeit, Einmessen, Steuerung) |

Jeder Check-in wird mit Uhrzeit (falls bekannt) im NVS gespeichert (`ci0`…`ci29`), ohne Namen, für eine spätere Elternseite im Heimnetz.

## Aufbau

```
core/        plattformunabhängig (C++17): Vektor-Renderer, Schriften, Synthesizer, Klopf-/Schüttel-Erkennung, Launcher
tiles/       checkin, grummel (Büro- und Kinder-Skin), breathe, balance, album, horses (gezeichnete Pferde), oracle (Quiz)
firmware/    Gerät: Rendern auf Kern 1, Display/Eingabe/Audio auf Kern 0, LittleFS, JPEG, serielle Befehle
lib/amoled18 Board-Schicht: Pins, Display (CO5300), Touch (CST816), IMU (QMI8658), Audio (ES8311)
sim/         Simulator für den Mac (SDL2)
tools/       mkfont.swift (Schriften), horses.py (Pferde-Album), quiz.py (Fragenpakete), serial_log.py
quiz/        Fragenpakete als JSON (für ein späteres Kinder-Quiz)
licenses/    Schriftlizenz (SIL OFL 1.1)
```

Der Renderer zeichnet in voller Auflösung (368×448) kantengeglättete Formen: Ellipsen mit Verlauf, wabernde Blobs, Leuchten, Bögen, Kapseln, abgerundete Rechtecke, Sterne, Bilder und Text. Gerastert wird in Bändern zu 16 Zeilen im internen RAM. Der Display-Task schiebt das vorige Band per QSPI-DMA, während Kern 1 das nächste rechnet. Alle Kacheln laufen mit 30 fps.

## Bauen

Simulator (braucht SDL2, z. B. `brew install sdl2`):

```bash
make -C sim && sim/dksim
```

Maus = Finger, Leertaste oder 1/2/3 = klopfen, T = Tastatur tippen, X = schütteln, Pfeiltasten = neigen, B = BOOT, P = Bildschirmfoto. `sim/dksim --screens <ordner>` spielt alle Abläufe durch und speichert Bilder.

Gerät (PlatformIO):

```bash
pio run -t upload
```

## Pferdefotos

Die Fotos sind **nicht** im Repository. `tools/horses.py fetch` holt zu jeder Rasse das Hauptbild des deutschen Wikipedia-Artikels von Wikimedia Commons. Es nimmt nur freie Lizenzen (CC0, CC BY, CC BY-SA, gemeinfrei), schneidet auf 328×196 zu und merkt sich Urheber und Lizenz. Auf jeder Karte steht der Bildnachweis, die Liste der ausgewählten Fotos steht in [tools/horses/BILDNACHWEIS.md](tools/horses/BILDNACHWEIS.md). Ohne Fotos zeigt das Album gezeichnete Pferde.

```bash
python3 -m venv tools/.venv && tools/.venv/bin/pip install pillow pyserial certifi
tools/.venv/bin/python tools/horses.py fetch      # Fotos holen
tools/.venv/bin/python tools/horses.py review     # Übersicht ansehen
tools/.venv/bin/python tools/horses.py upload     # aufs Gerät (LittleFS)
```

## Serielle Befehle

115200 Baud, eine Zeile pro Befehl, z. B. `tools/.venv/bin/python tools/serial_log.py 10 status`:

| Befehl | Wirkung |
|---|---|
| `status` | Fragenpaket, Album, Dateisystem, Uhrzeit, Kachel, Neigung |
| `log` | Zwei-Sekunden-Protokoll (fps, Zeiten, Ruckler) ein/aus, startet aus |
| `kachel N` | zu Kachel N (0 Check-in, 1 Grummel, 2 Atmen, 3 Kugel ins Loch, 4 Pferde-Album) |
| `tipp X Y` | Tippen an Display-Position X, Y |
| `klopf N` | N künstliche Schläge im Abstand von 250 ms |
| `imu`, `imuraw`, `imureg R V` | Messwerte, Rohregister, Register schreiben (Fehlersuche) |
| `bench`, `prof` | Rasterzeit je Form, Rechenzeit je Formtyp |
| `{"time":N}` | Uhrzeit setzen (Unix-Sekunden, Ortszeit) |
| `{"file":"begin",…}`, `F <base64>`, `{"file":"end"}` | Datei ins LittleFS schreiben |

## Stolperfallen auf der Hardware

- **Serielle Ausgabe darf nicht blockieren:** Liest am Rechner niemand mit, läuft der USB-Puffer voll und jedes `Serial.print` wartet. Gemessen wurden Stillstände bis 6 s. Deshalb `Serial.setTxTimeoutMs(0)` und das Protokoll nur auf Befehl.
- **Gleitkomma ist teuer:** Addition und Multiplikation kosten etwa 5 Takte, `sqrtf`, Division, `floorf`/`ceilf`, `fminf`/`fmaxf` und 64-Bit-Multiplikation als Bibliotheksaufrufe 60 bis 90 Takte. In Pixel- und Zeilenschleifen deshalb Ganzzahl-Differenzen, eigene `fsqrt`/`ffloor`/`fmn`, Kehrwerte vorab.
- **`-O2` nur für `core/render.cpp`** (Pragma in der Datei), sonst `-Os`.
- **Das Panel erwartet Big Endian:** `rasterize(..., true)` tauscht die Bytes am Bandende.
- **IMU:** Auf dem Testgerät war der QMI8658 defekt. Eine Achse hing fest, und Neigen kam kaum an. Klopfen und Schütteln funktionieren trotzdem, weil sie nur kurze Änderungen auswerten. Das Ballspiel nutzt deshalb standardmäßig den Finger.
- **JPEGDEC** dekodiert nur Baseline-JPEG. `horses.py` speichert entsprechend.

## Lizenzen

- Schriften: Nunito und Lilita One, SIL Open Font License 1.1, siehe [licenses/OFL-1.1.txt](licenses/OFL-1.1.txt). Die Bitmap-Fassungen in `core/font_*.h` sind daraus abgeleitet.
- ES8311-Treiber (`lib/amoled18/es8311*`): Espressif, Apache-2.0.
- Pferdefotos: nicht enthalten, siehe oben.
