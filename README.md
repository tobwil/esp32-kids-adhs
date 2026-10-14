# esp32-kids-adhs

Ein ruhiger Begleiter für Kinder auf dem **Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)**. Er soll helfen, mit „Wut im Bauch“ umzugehen: Gefühle benennen, Wut wegfüttern, gemeinsam atmen. Dazu kommen ein ruhiges Geschicklichkeitsspiel, Rätsel für die 2. Klasse und ein Pferde-Album zum Sammeln. Es ist eine Unterstützung im Alltag, keine Therapie.

![Kinderversion im Simulator](doc/kinder-ablauf.png)

## Dashboard und Apps

Das Gerät startet schwarz, ein Tipp weckt es. Startseite ist das **Dashboard**. Oben wohnt Grummel als kleines Haustier, ähnlich wie ein Tamagotchi, aber ohne Druck:

- Am Anfang liegt ein **Ei** da. Streicheln, Antippen und sanftes Schütteln wärmen es, bis Grummel schlüpft.
- Er **wächst** in drei Stufen (Baby, Kind, groß) durch Pflege: füttern, streicheln, spielen. Die Übungen in den Apps zählen mit.
- Oben stehen drei **Bedürfnisse** als Ringe: Apfel (antippen füttert, der Apfel fliegt in den Mund), Herz (streicheln) und Ball (öffnet das Kugelspiel). Sie leeren sich langsam über Stunden.
- Er reagiert aufs Streicheln (Herzchen steigen auf), Antippen (kichert), Schütteln (Kringelaugen), Klopfen (erschrickt kurz) und auf Stimme (hört zu). Vom Mikrofon wird nur der Pegel ausgewertet, nichts wird aufgenommen.
- Ab und zu fragt er „Wie geht's dir?“. Ein Tipp auf die Blase öffnet den Check-in. Ist die Uhrzeit bekannt, schläft er von 20 bis 7 Uhr.
- Mit den Sternen bekommt er Schmuck: ab 10 eine Schleife, ab 25 eine Blume, ab 50 eine Krone.

Regeln für das Haustier: Grummel stirbt nie, wird nie krank und macht keine Vorwürfe. Leere Bedürfnisse heißen nur, dass er freundlich um etwas bittet.

Unter Grummel liegen die App-Symbole. Jede App öffnet bildschirmfüllend. Wischen nach oben oder unten blättert von App zu App, Wischen zur Seite oder das ✕ oben rechts schließt sie. Ein Abzeichen am Pferde-Symbol zeigt, dass eine neue Karte wartet.

| App | Was passiert |
|---|---|
| Gefühle (Wie geht's dir?) | fünf Gesichter (wütend, kribbelig, traurig, müde, froh). Das Gerät schickt zum passenden Werkzeug und fragt danach noch einmal nach |
| Grummel | schnell aufs Display tippen, auf den Tisch klopfen oder schütteln: Wutfunken fliegen in Grummel, er wird satt, rülpst, verwandelt die Wut in Blasen und atmet mit („Riech an der Blume“, „Puste die Kerze aus“) |
| Atmen | ein Ballon atmet mit: 4 s ein, 5 s aus, 4 Runden |
| Kugel ins Loch | Kugeln ins Loch rollen, per Finger als Joystick oder per Neigen. Ohne Zeitdruck, nach 4 Runden kommt eine Pause |
| Rätsel | 5 Fragen pro Runde für die 2. Klasse (Rechnen bis 100, Zählen mit Bild, Tiere und Natur, Zeit, Sprache, Körper). Bei Zählbildern hilft Antippen beim Mitzählen. Ab drei gesammelten Pferdekarten kommt eine Bonusfrage „Welches Pferd ist das?“ mit Foto aus dem Album. Bei falscher Antwort zeigt das Gerät freundlich die Lösung und wartet. Ein Stern pro Runde, egal wie viele richtig waren |
| Pferde-Album | 24 Pferderassen als Sammelkarten mit Foto, Typ, Herkunft, Größe, Farben, Wesen und „Wusstest du?“. Alle 3 Sterne aus den Übungen gibt es eine neue Karte, nie doppelt |

Sterne gibt es fürs Benutzen der Werkzeuge, nicht fürs „Bravsein“. Gestaltungsregeln: ruhige Bilder, nichts blinkt, kurze Runden ohne Zeitdruck, keine Ranglisten, freundliche Rückmeldungen, Ton startet aus.

## Bedienung

| Geste | Wirkung |
|---|---|
| Tippen, wenn das Display schwarz ist | aufwecken (das Gerät startet schwarz und geht nach 3 min Ruhe wieder aus) |
| App-Symbol antippen | App öffnen |
| nach oben wischen | nächste App (vom Dashboard aus: die erste) |
| nach unten wischen | vorige App, vor der ersten kommt das Dashboard |
| nach links oder rechts wischen, ✕ oben rechts | App schließen, zurück zum Dashboard |
| 3 s lang drücken oder BOOT | Einstellungen für Eltern (Ton, Klopf-Empfindlichkeit, Einmessen, Steuerung) |

Beim Kugelspiel steuert der Finger die Kugel. Dort wirkt Wischen nur, wenn es am Bildschirmrand beginnt, ebenso auf Grummel selbst (dort ist es Streicheln). Gedimmt reagiert das Gerät normal, nur aus dem Schwarzen heraus weckt die erste Berührung bloß auf.

Apps, die aus „Wie geht's dir?“ heraus geöffnet wurden, kehren danach dorthin zurück („Und wie fühlt sich dein Bauch jetzt an?“).

Jeder Check-in wird mit Uhrzeit (falls bekannt) im NVS gespeichert (`ci0`…`ci29`), ohne Namen, für eine spätere Elternseite im Heimnetz.

## Aufbau

```
core/        plattformunabhängig (C++17): Vektor-Renderer, Schriften, Synthesizer, Klopf-/Schüttel-Erkennung, Launcher
tiles/       home (Dashboard), pet (Haustier-Zustand), checkin, grummel (Büro- und Kinder-Skin), breathe, balance, riddle (Rätsel),
             album, horses (gezeichnete Pferde), icons, oracle (Erwachsenen-Quiz, nicht in der Reihe)
firmware/    Gerät: Rendern auf Kern 1, Display/Eingabe/Audio auf Kern 0, LittleFS, JPEG, serielle Befehle
lib/amoled18 Board-Schicht: Pins, Display (CO5300), Touch (CST816), IMU (QMI8658), Audio (ES8311)
sim/         Simulator für den Mac (SDL2)
tools/       mkfont.swift (Schriften), horses.py (Pferde-Album), quiz.py (Fragenpakete), serial_log.py
quiz/        Fragenpakete als JSON: kinder-2klasse (Rätsel-App, eingebaut), 2026-10-a (Erwachsene)
licenses/    Schriftlizenz (SIL OFL 1.1)
```

Der Renderer zeichnet in voller Auflösung (368×448) kantengeglättete Formen: Ellipsen mit Verlauf, wabernde Blobs, Leuchten, Bögen, Kapseln, abgerundete Rechtecke, Sterne, Bilder und Text. Gerastert wird in Bändern zu 16 Zeilen im internen RAM. Der Display-Task schiebt das vorige Band per QSPI-DMA, während Kern 1 das nächste rechnet. Alle Kacheln laufen mit 30 fps.

## Bauen

Simulator (braucht SDL2, z. B. `brew install sdl2`):

```bash
make -C sim && sim/dksim
```

Maus = Finger, Leertaste oder 1/2/3 = klopfen, T = Tastatur tippen, X = schütteln, M = sprechen, Pfeiltasten = neigen, B = BOOT, P = Bildschirmfoto. `sim/dksim --screens <ordner>` spielt alle Abläufe durch, speichert Bilder und meldet jeden Text, der über den Rand, in die runden Ecken oder unter das ✕ ragt.

Gerät (PlatformIO):

```bash
pio run -t upload
```

## Rätsel-Fragen austauschen

Die Fragen stehen in [quiz/kinder-2klasse.json](quiz/kinder-2klasse.json): drei Antworten, die richtige zuerst (das Gerät mischt), dazu eine kurze Erklärung. `"bild": "zaehl:7:apfel"` zeigt sieben Äpfel zum Zählen (auch `stern`, `herz`, `ball`, 1 bis 12 Stück). `quiz.py check` prüft Längen, Zeichen und ob das Bild zur Antwort passt.

```bash
tools/.venv/bin/python tools/quiz.py check  quiz/kinder-2klasse.json
tools/.venv/bin/python tools/quiz.py upload quiz/kinder-2klasse.json      # aufs Gerät, ohne neu zu flashen
tools/.venv/bin/python tools/quiz.py build  quiz/kinder-2klasse.json core/quiz_kids.h QUIZ_KIDS   # als eingebautes Paket
```

## Pferdefotos

Die Fotos sind **nicht** im Repository. `tools/horses.py fetch` holt zu jeder Rasse das Hauptbild des deutschen Wikipedia-Artikels von Wikimedia Commons. Es nimmt nur freie Lizenzen (CC0, CC BY, CC BY-SA, gemeinfrei), schneidet auf 328×196 zu und merkt sich Urheber und Lizenz. Auf jeder Karte steht der Bildnachweis, die Liste der ausgewählten Fotos steht in [tools/horses/BILDNACHWEIS.md](tools/horses/BILDNACHWEIS.md). Ohne Fotos zeigt das Album gezeichnete Pferde.

Eigene Bilder statt der Commons-Fotos: Zu jeder Rasse steht in [tools/horses/BILDPROMPTS.md](tools/horses/BILDPROMPTS.md) eine Bildbeschreibung (ganzes Pferd von der Seite, Querformat, viel Rand), z. B. für einen Bildgenerator oder als Vorlage für eigene Fotos. `horses.py import <ordner>` übernimmt Dateien namens `<Nr>.png` oder `<Nr>-name.jpg` (Nummer wie in der Liste): Das ganze Pferd bleibt sichtbar, der Rand wird mit einer unscharfen Fassung des Bildes gefüllt, und auf der Karte steht „eigenes Bild“.

```bash
python3 -m venv tools/.venv && tools/.venv/bin/pip install pillow pyserial certifi
tools/.venv/bin/python tools/horses.py fetch      # Fotos holen
tools/.venv/bin/python tools/horses.py import bilder/   # oder: eigene Bilder übernehmen
tools/.venv/bin/python tools/horses.py review     # Übersicht ansehen
tools/.venv/bin/python tools/horses.py upload     # aufs Gerät (LittleFS)
```

## Serielle Befehle

115200 Baud, eine Zeile pro Befehl, z. B. `tools/.venv/bin/python tools/serial_log.py 10 status`:

| Befehl | Wirkung |
|---|---|
| `status` | Fragenpaket, Album, Dateisystem, Uhrzeit, Kachel, Mikrofonpegel, Neigung |
| `log` | Zwei-Sekunden-Protokoll (fps, Zeiten, Ruckler) ein/aus, startet aus |
| `kachel N` | zu Kachel N (0 Dashboard, 1 Gefühle, 2 Grummel, 3 Atmen, 4 Kugel ins Loch, 5 Pferde-Album, 6 Rätsel) |
| `neustart-kind` | Spielstände auf Anfang (Sterne, Karten, Check-ins, Ei), Einstellungen bleiben, dann Neustart |
| `tipp X Y` | Tippen an Display-Position X, Y |
| `wisch X0 Y0 X1 Y1` | Wischen von X0, Y0 nach X1, Y1 |
| `beruehrung` | jede Berührung mit Position ausgeben ein/aus |
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
