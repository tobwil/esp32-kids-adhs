// Desk-Kacheln – Geräteseite für Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)
//
// Aufteilung:  Kern 1 = App und Rastern (30 fps, loop()), Band für Band (16 Zeilen) in internen DMA-Puffern
//              Kern 0 = Display-Task (Bänder per QSPI schieben), Eingabe-Task (IMU ~500 Hz, Touch 100 Hz), Audio-Task
//
// Serielle Befehle (115200 Baud, eine Zeile):
//   status         Fragenpaket, Dateisystem, Uhrzeit, Kachel
//   neustart-kind  Spielstände (Sterne, Karten, Check-ins, Grummel) auf Anfang, Einstellungen bleiben
//   log            Zwei-Sekunden-Protokoll (fps, Zeiten, Ruckler) ein/aus, startet aus
//   imu            Messwerte je Frame ein/aus (Spitze, Grundunruhe, erkannte Schläge) zum Kalibrieren
//   {"time":N}     Uhrzeit setzen (Unix-Sekunden, Ortszeit), später vom Companion
//   {"quiz":"begin","pack":"…","count":N}, dann je Frage „Q <zeile>“, dann {"quiz":"end"}
//                  Fragenpaket übertragen (tools/quiz.py upload), jede Zeile wird mit „quiz ok“ bestätigt
//   {"quiz":"reset"}  übertragenes Paket löschen, zurück zum eingebauten
//   {"file":"begin","path":"/pferde/3.jpg","size":N}, dann „F <base64>“ (je ≤ 512 Byte), dann {"file":"end"}
//                  Datei ins LittleFS schreiben (tools/horses.py upload); {"album":"reload"} lädt Karten und Fotos neu
//   klopf N        N künstliche Schläge im Abstand von 250 ms (Test ohne Tisch)
//   kachel N       zur Kachel N wechseln (0 Dashboard, 1 Check-in, 2 Grummel, 3 Atmen, 4 Kugel ins Loch, 5 Pferde-Album, 6 Rätsel, 7 Radio)
//   tipp X Y       Tippen an Display-Position X, Y (Test ohne Finger)
//   wisch X0 Y0 X1 Y1  Wischen von X0, Y0 nach X1, Y1
//   boot           BOOT-Taste drücken (Einstellungen auf/zu)
//   beruehrung     jede Berührung mit Position ausgeben ein/aus (zum Prüfen der Touch-Koordinaten)
//   bench          Rasterzeit je Form messen
//   prof           Rechenzeit je Formtyp im Zwei-Sekunden-Protokoll ein/aus
#include <Arduino.h>
#include <LittleFS.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <mbedtls/base64.h>

#include <cmath>

#include "amoled18.h"
#include "app.h"
#include "balance.h"
#include "breathe.h"
#include "album.h"
#include "album_builtin.h"
#include "checkin.h"
#include "home.h"
#include "grummel.h"
#include "net.h"
#include "portal_sync.h"
#include "quiz_kids.h"
#include "radio.h"
#include "rewards.h"
#include "riddle.h"
#include "fonts.h"

using namespace dk;
namespace hw = amoled18;

// Lage der IMU auf der Platine → Bildschirmachsen (x rechts, y oben, z aus dem Bildschirm), wie bei Hallen-Legenden
#define IMU_X_SRC 0   // 0 = Sensor-x, 1 = Sensor-y, 2 = Sensor-z
#define IMU_X_SIGN 1
#define IMU_Y_SRC 1
#define IMU_Y_SIGN 1
#define IMU_Z_SRC 2
#define IMU_Z_SIGN 1

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ---------------- Speicher (NVS) ----------------
struct NvsStore : Store {
  Preferences prefs;
  void begin() { prefs.begin("deskkach", false); }
  int get(const char* k, int def) override { return prefs.getInt(k, def); }
  void set(const char* k, int v) override { prefs.putInt(k, v); }
};

static Synth synth;
static NvsStore store;
static App* app;
static Scene scene;
// Kinder-Aufbau: 0 Dashboard, 1 Check-in, 2 Grummel, 3 Atmen, 4 Kugel ins Loch, 5 Pferde-Album, 6 Rätsel, 7 Radio.
enum { T_HOME, T_CHECKIN, T_GRUMMEL, T_BREATHE, T_BALANCE, T_ALBUM, T_RIDDLE, T_RADIO };
static const Home::AppIcon APPS[] = {{"Gefühle", T_CHECKIN, 0}, {"Grummel", T_GRUMMEL, 1}, {"Atmen", T_BREATHE, 2}, {"Kugel", T_BALANCE, 3},
                                     {"Pferde", T_ALBUM, 4},    {"Rätsel", T_RIDDLE, 5},    {"Radio", T_RADIO, 6}};
static RadioPlayer* radioPlayer = nullptr;   // im PSRAM (Ringpuffer 64 KB)
static PortalSync portalSync;
static Home* home;
static bool fsOk = false;
static Riddle* riddle = nullptr;
static CheckIn* checkin;
static Grummel* grummel;
static Breathe* breathe;
static Balance* balance;
static Album* album;

// ---------------- Pferdefotos: JPEG aus LittleFS (/pferde/<id>.jpg), dekodiert in PSRAM ----------------
struct FlashImages : ImageStore {
  JPEGDEC jpeg;
  struct Slot { int id = -1; uint16_t* px = nullptr; };
  Slot thumbs[48];
  Slot full;
  uint16_t* target = nullptr;
  int targetW = 0;
  bool fullOk = false;
  int decOk = 0, decFail = 0;
  uint32_t decUs = 0;

  static int draw(JPEGDRAW* d) {   // Block in den Zielpuffer kopieren
    auto* self = static_cast<FlashImages*>(d->pUser);
    for (int y = 0; y < d->iHeight; y++)
      memcpy(self->target + (d->y + y) * self->targetW + d->x, d->pPixels + y * d->iWidth, d->iWidth * 2);
    return 1;
  }
  bool decode(int id, int scale, int w, int h, uint16_t* out) {
    char path[32];
    snprintf(path, sizeof path, "/pferde/%d.jpg", id);
    if (!fsOk || !LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    const size_t n = f.size();
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM));
    if (!buf) { f.close(); return false; }
    f.read(buf, n);
    f.close();
    bool ok = false;
    if (jpeg.openRAM(buf, int(n), draw)) {
      if (jpeg.getWidth() / scale == w && jpeg.getHeight() / scale == h) {
        target = out; targetW = w;
        jpeg.setUserPointer(this);
        jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
        const uint32_t t0 = micros();
        ok = jpeg.decode(0, 0, scale == 2 ? JPEG_SCALE_HALF : 0) == 1;
        decUs = micros() - t0;
      }
      jpeg.close();
    }
    heap_caps_free(buf);
    (ok ? decOk : decFail)++;
    return ok;
  }
  Image thumb(int id) override {
    for (auto& s : thumbs) if (s.id == id) return s.px ? Image{s.px, THUMB_W, THUMB_H} : Image{};
    for (auto& s : thumbs) {
      if (s.id != -1) continue;
      s.id = id;   // auch bei Fehlschlag merken, damit nicht jedes Frame neu versucht wird
      uint16_t* px = static_cast<uint16_t*>(heap_caps_malloc(THUMB_W * THUMB_H * 2, MALLOC_CAP_SPIRAM));
      if (px && decode(id, 2, THUMB_W, THUMB_H, px)) s.px = px;
      else if (px) heap_caps_free(px);
      return s.px ? Image{s.px, THUMB_W, THUMB_H} : Image{};
    }
    return {};
  }
  Image card(int id) override {
    if (full.id == id) return full.px && fullOk ? Image{full.px, CARD_W, CARD_H} : Image{};
    if (!full.px) full.px = static_cast<uint16_t*>(heap_caps_malloc(CARD_W * CARD_H * 2, MALLOC_CAP_SPIRAM));
    full.id = id;
    fullOk = full.px && decode(id, 1, CARD_W, CARD_H, full.px);
    return fullOk ? Image{full.px, CARD_W, CARD_H} : Image{};
  }
  void reload() override {
    for (auto& s : thumbs) { if (s.px) heap_caps_free(s.px); s = Slot{}; }
    full.id = -1;
  }
};
static FlashImages images;
static char* albumText = nullptr;

static void loadAlbum() {
  char* text = nullptr;
  if (fsOk && LittleFS.exists("/pferde/karten.txt")) {
    File f = LittleFS.open("/pferde/karten.txt", "r");
    const size_t n = f.size();
    text = static_cast<char*>(heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM));
    if (text) { f.readBytes(text, n); text[n] = 0; }
    f.close();
  }
  if (!text || !album->load(text)) album->load(ALBUM_BUILTIN);   // Album kopiert den Text
  if (text) heap_caps_free(text);
  images.reload();
  Serial.printf("album: %d Karten, %d aufgedeckt\n", album->count(), album->owned());
}

// ---------------- Fragenpaket (LittleFS /quiz.txt, sonst eingebaut) ----------------
static QuizPack pack;
static char* packText = nullptr;   // PSRAM

static void loadPack() {
  char* text = nullptr;
  if (fsOk && LittleFS.exists("/quiz.txt")) {
    File f = LittleFS.open("/quiz.txt", "r");
    const size_t n = f.size();
    text = static_cast<char*>(heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM));
    if (text) { f.readBytes(text, n); text[n] = 0; }
    f.close();
  }
  QuizPack fresh;
  if (!text || !fresh.load(text)) {
    if (text) heap_caps_free(text);
    text = nullptr;
    pack.load(QUIZ_KIDS);
  } else {
    pack.load(text);
  }
  if (packText) heap_caps_free(packText);
  packText = text;
  if (riddle) riddle->setPack(&pack);
  Serial.printf("fragen: %d (%s, %s)\n", pack.count(), pack.id(), text ? "übertragen" : "eingebaut");
}

// ---------------- Eingabe (Kern 0) ----------------
static portMUX_TYPE inMux = portMUX_INITIALIZER_UNLOCKED;
static TouchSample touchQ[32];
static volatile bool touchLog = false;
static int fakeBoot = 0;   // serieller Befehl „boot“: Taste für ein paar Frames gedrückt   // „beruehrung“: Aufsetzen und Loslassen mit Position ausgeben
static int touchN = 0;
static AccelSample accQ[128];
static int accHead = 0, accCount = 0;
static volatile uint32_t imuReads = 0, imuFails = 0;

static void inputTask(void*) {
  bool wasDown = false;
  int16_t lx = 0, ly = 0;
  uint32_t lastTouch = 0;
  for (;;) {
    const uint32_t now = millis();
    float v[3];
    if (hw::readAccel(v)) {
      imuReads = imuReads + 1;
      AccelSample s{IMU_X_SIGN * v[IMU_X_SRC], IMU_Y_SIGN * v[IMU_Y_SRC], IMU_Z_SIGN * v[IMU_Z_SRC], now};
      portENTER_CRITICAL(&inMux);
      accQ[(accHead + accCount) % 128] = s;
      if (accCount < 128) accCount++;
      else accHead = (accHead + 1) % 128;
      portEXIT_CRITICAL(&inMux);
    } else if (hw::imuOk()) imuFails = imuFails + 1;

    if (now - lastTouch >= 10) {
      lastTouch = now;
      bool down = false;
      int16_t x = lx, y = ly;
      if (hw::readTouch(down, x, y) && (down || wasDown)) {
        if (!down) { x = lx; y = ly; }
        if (touchLog && down != wasDown) Serial.printf("beruehrung %s %d %d\n", down ? "auf" : "los", x, y);
        TouchSample s{down, x, y, now};
        portENTER_CRITICAL(&inMux);
        if (touchN < 32) touchQ[touchN++] = s;
        else touchQ[31] = s;
        portEXIT_CRITICAL(&inMux);
        wasDown = down;
        lx = x; ly = y;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ---------------- Display (Kern 0) ----------------
static constexpr int BAND = 16, NBUF = 4;
struct Band { uint16_t* buf; int16_t y; };
static QueueHandle_t freeQ, fullQ;
static volatile int brightness = 0, curBrightness = -1;
static volatile uint32_t pushUs = 0, pushBusyUs = 0;

static void displayTask(void*) {
  auto* gfx = hw::panel();
  uint32_t t0 = 0, busy = 0;
  for (;;) {
    Band b;
    xQueueReceive(fullQ, &b, portMAX_DELAY);
    if (b.y == 0 || !b.buf) {
      if (curBrightness != brightness) { curBrightness = brightness; gfx->setBrightness(curBrightness); }
      if (!b.buf) continue;   // nur Helligkeit setzen
      t0 = micros();
      busy = 0;
      gfx->startWrite();
      gfx->writeAddrWindow(0, 0, hw::LCD_W, hw::LCD_H);
    }
    const uint32_t tb = micros();
    hw::bus()->writeBytes(reinterpret_cast<uint8_t*>(b.buf), W * BAND * 2);
    busy += micros() - tb;
    if (b.y + BAND >= H) {
      gfx->endWrite();
      pushUs = micros() - t0;
      pushBusyUs = busy;
    }
    xQueueSend(freeQ, &b.buf, portMAX_DELAY);
  }
}

// ---------------- Audio (Kern 0) ----------------
static void audioTask(void*) {
  static int16_t mono[256];
  static int16_t stereo[512];
  for (;;) {
    synth.render(mono, 256);
    if (radioPlayer) radioPlayer->mix(mono, 256);
    for (int i = 0; i < 256; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
    hw::i2s().write(reinterpret_cast<uint8_t*>(stereo), sizeof stereo);
  }
}

// ---------------- Messung (serieller Befehl „bench“) ----------------
static void bench() {   // Zeit je Form über alle 28 Bänder
  uint16_t* t = static_cast<uint16_t*>(heap_caps_malloc(W * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  auto bench = [&](const char* name, auto fn) {
    Scene sc;
    fn(sc);
    uint32_t a = micros();
    for (int y = 0; y < H; y += BAND) sc.rasterize(t, y, BAND);
    Serial.printf("bench %-16s %6lu us\n", name, (unsigned long)(micros() - a));
  };
  bench("leer", [](Scene&) {});
  bench("ring deckend", [](Scene& s) { s.ring(184, 214, 118, 14, hex(0x808080)); });
  bench("ring 80%", [](Scene& s) { s.ring(184, 214, 118, 14, hex(0x808080), 0.8f); });
  bench("bogen 1 rad", [](Scene& s) { s.arc(184, 214, 118, 14, 0, 1, hex(0x808080)); });
  bench("kreis r100", [](Scene& s) { s.circle(184, 214, 100, hex(0x808080)); });
  bench("kreis r100 50%", [](Scene& s) { s.circle(184, 214, 100, hex(0x808080), 0.5f); });
  bench("glow r100", [](Scene& s) { s.glow(184, 214, 100, hex(0x808080), 0.5f); });
  bench("pille 320x58", [](Scene& s) { s.rrect(24, 222, 320, 58, 29, hex(0x2a2540)); });
  bench("text", [](Scene& s) { s.text(fonts::small, "Companion-App noch nicht verbunden", 184, 380, hex(0x808080), 1, Align::Center); });
  bench("ring r150 d7", [](Scene& s) { s.ring(184, 214, 150, 7, hex(0xff9a5a)); });
  bench("grummel-ring", [](Scene& s) { s.ring(184, 214, 150, 6, hex(0x2a2440), 0.7f); s.arc(184, 214, 150, 7, 0, 4, hex(0xff9a5a)); });
  heap_caps_free(t);
}

// ---------------- Mikrofon (Kern 0): nur der Pegel, nichts wird gespeichert ----------------
static volatile float voiceLevel = 0, micDb = -120;
static void micTask(void*) {
  static int16_t buf[512];   // 256 Stereo-Frames ≈ 16 ms
  float floorDb = 0, level = 0;
  int count = 0;
  for (;;) {
    const size_t n = hw::i2s().readBytes(reinterpret_cast<char*>(buf), sizeof buf) / 4;
    if (!n) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    double sum = 0;
    for (size_t i = 0; i < n; i++) { const float v = buf[2 * i]; sum += double(v) * v; }
    const float db = 10 * log10f(float(sum / n) + 1e-3f) - 90.3f;   // dBFS
    micDb = db;
    // Grundgeräusch langsam nachführen, Sprache = deutlich darüber
    if (++count < 60) floorDb = count == 1 ? db : floorDb + 0.2f * (db - floorDb);   // erste Sekunde: Grundgeräusch einlesen
    else floorDb += (db < floorDb ? 0.2f : 0.002f) * (db - floorDb);
    const float above = (db - floorDb - 8) / 20;   // ab 8 dB über dem Grundgeräusch, voll bei +28 dB
    const float target = above < 0 ? 0 : above > 1 ? 1 : above;
    level += (target - level) * (target > level ? 0.5f : 0.1f);
    voiceLevel = level;
  }
}

// ---------------- Serielle Befehle ----------------
static int64_t epochBase = 0;   // Unix-Sekunden bei millis() == 0
static bool imuStream = false;
static bool prof = false;
static bool logOn = false;   // Zwei-Sekunden-Protokoll, Befehl „log“
static int fakeKnocks = 0;
static uint32_t fakeNext = 0;

static File quizFile, upFile;
static int quizLines = 0;
static size_t upBytes = 0;

static void pollSerial() {
  static char line[1024];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < int(sizeof line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    if (!strncmp(line, "F ", 2)) {   // Datei-Upload: base64-Block
      if (!upFile) { Serial.println("file fehler: kein begin"); continue; }
      uint8_t bin[600];
      size_t olen = 0;
      if (mbedtls_base64_decode(bin, sizeof bin, &olen, reinterpret_cast<const uint8_t*>(line + 2), strlen(line + 2)) != 0) { Serial.println("file fehler: base64"); continue; }
      upFile.write(bin, olen);
      upBytes += olen;
      Serial.println("file ok");
    } else if (strstr(line, "\"file\"") && strstr(line, "\"begin\"")) {
      const char* p = strstr(line, "\"path\"");
      char path[48] = "";
      if (p && (p = strchr(p + 6, '"'))) { p++; int k = 0; while (*p && *p != '"' && k < 47) path[k++] = *p++; path[k] = 0; }
      if (!fsOk || path[0] != '/') { Serial.println("file fehler: pfad oder dateisystem"); continue; }
      if (char* slash = strrchr(path, '/'); slash && slash != path) { *slash = 0; LittleFS.mkdir(path); *slash = '/'; }
      if (upFile) upFile.close();
      upFile = LittleFS.open(path, "w");
      upBytes = 0;
      Serial.println(upFile ? "file ok" : "file fehler: öffnen");
    } else if (strstr(line, "\"file\"") && strstr(line, "\"end\"")) {
      if (upFile) upFile.close();
      Serial.printf("file fertig %u\n", unsigned(upBytes));
    } else if (strstr(line, "\"album\"") && strstr(line, "\"reload\"")) {
      loadAlbum();
      Serial.printf("album %d Karten\n", album->count());
    } else if (!strncmp(line, "Q ", 2)) {   // Zeile eines Fragenpakets
      if (!quizFile) { Serial.println("quiz fehler: kein begin"); continue; }
      quizFile.print(line + 2);
      quizFile.print('\n');
      quizLines++;
      Serial.println("quiz ok");
    } else if (strstr(line, "\"quiz\"") && strstr(line, "\"begin\"")) {
      if (!fsOk) { Serial.println("quiz fehler: kein Dateisystem"); continue; }
      if (quizFile) quizFile.close();
      quizFile = LittleFS.open("/quiz.tmp", "w");
      const char* p = strstr(line, "\"pack\"");
      char id[32] = "unbenannt";
      if (p && (p = strchr(p + 6, '"'))) { p++; int k = 0; while (*p && *p != '"' && k < 31) id[k++] = *p++; id[k] = 0; }
      quizFile.printf("#pack %s\n", id);
      quizLines = 0;
      Serial.println(quizFile ? "quiz ok" : "quiz fehler: Datei");
    } else if (strstr(line, "\"quiz\"") && strstr(line, "\"end\"")) {
      if (!quizFile) { Serial.println("quiz fehler: kein begin"); continue; }
      quizFile.close();
      LittleFS.remove("/quiz.txt");
      LittleFS.rename("/quiz.tmp", "/quiz.txt");
      loadPack();
      Serial.printf("quiz fertig %d %s\n", pack.count(), pack.id());
    } else if (strstr(line, "\"quiz\"") && strstr(line, "\"reset\"")) {
      if (fsOk) LittleFS.remove("/quiz.txt");
      loadPack();
      Serial.printf("quiz fertig %d %s\n", pack.count(), pack.id());
    } else if (!strncmp(line, "tipp ", 5)) {   // künstlicher Tipp: runter und nach 80 ms wieder hoch
      int x = 0, y = 0;
      if (sscanf(line + 5, "%d %d", &x, &y) == 2) {
        portENTER_CRITICAL(&inMux);
        if (touchN < 30) {
          touchQ[touchN++] = {true, int16_t(x), int16_t(y), uint32_t(millis())};
          touchQ[touchN++] = {false, int16_t(x), int16_t(y), uint32_t(millis() + 80)};
        }
        portEXIT_CRITICAL(&inMux);
      }
    } else if (!strncmp(line, "wisch ", 6)) {   // künstlicher Wisch in 8 Schritten über 160 ms: wisch X0 Y0 X1 Y1
      int x0, y0, x1, y1;
      if (sscanf(line + 6, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
        portENTER_CRITICAL(&inMux);
        const uint32_t t0 = millis();
        for (int k = 0; k <= 8 && touchN < 31; k++) touchQ[touchN++] = {true, int16_t(x0 + (x1 - x0) * k / 8), int16_t(y0 + (y1 - y0) * k / 8), t0 + 20u * k};
        touchQ[touchN++] = {false, int16_t(x1), int16_t(y1), t0 + 180};
        portEXIT_CRITICAL(&inMux);
      }
    } else if (!strcmp(line, "boot")) {   // BOOT-Taste drücken (Einstellungen auf/zu)
      fakeBoot = 3;
    } else if (!strcmp(line, "beruehrung")) {
      touchLog = !touchLog;
      Serial.printf("beruehrung %s\n", touchLog ? "an" : "aus");
    } else if (!strcmp(line, "status")) {
      Serial.printf("status fragen %d (%s, %s)  dateisystem %s  zeit %lld  laufzeit %lu s\n", pack.count(), pack.id(), packText ? "übertragen" : "eingebaut",
                    fsOk ? "ok" : "fehlt", (long long)(epochBase ? epochBase + millis() / 1000 : 0), (unsigned long)(millis() / 1000));
      Serial.println(app->debug());
      Serial.printf("album %d Karten, %d aufgedeckt, Fotos dekodiert %d, Fehler %d, zuletzt %lu us\n", album->count(), album->owned(), images.decOk, images.decFail, (unsigned long)images.decUs);
      Serial.printf("mikrofon %.1f dBFS, stimme %.2f\n", float(micDb), float(voiceLevel));
      Serial.printf("neigung x %.2f y %.2f z %.2f g\n", app->motion.tiltX, app->motion.tiltY, app->motion.tiltZ);
      char ns[160];
      netStatus(ns, sizeof ns);
      Serial.printf("%s, sender %d, %s\n", ns, radioPlayer->count(), radioPlayer->playing() ? "spielt" : "still");
    } else if (!strncmp(line, "imureg ", 7)) {   // Testweise ein IMU-Register schreiben: imureg <reg hex> <wert hex>
      unsigned reg = 0, val = 0;
      if (sscanf(line + 7, "%x %x", &reg, &val) == 2) { hw::i2cWriteReg(0x6B, uint8_t(reg), uint8_t(val)); Serial.printf("imureg %02X = %02X\n", reg, val); }
    } else if (!strcmp(line, "imuraw")) {   // Rohwerte und Konfiguration der IMU
      uint8_t r[14] = {}, c[9] = {};
      hw::i2cRead(0x6B, 0x2D, r, 14);
      hw::i2cRead(0x6B, 0x00, c, 9);
      Serial.printf("imuraw status %02X %02X  ax %d ay %d az %d  (bytes", r[0], r[1], int16_t(r[8] | r[9] << 8), int16_t(r[10] | r[11] << 8), int16_t(r[12] | r[13] << 8));
      for (int i = 0; i < 14; i++) Serial.printf(" %02X", r[i]);
      Serial.print(")  ctrl");
      for (int i = 0; i < 9; i++) Serial.printf(" %02X", c[i]);
      Serial.println();
      uint8_t b[6];
      for (int i = 0; i < 6; i++) hw::i2cRead(0x6B, uint8_t(0x35 + i), &b[i], 1);   // einzeln statt am Stück
      Serial.printf("imuraw einzeln ax %d ay %d az %d\n", int16_t(b[0] | b[1] << 8), int16_t(b[2] | b[3] << 8), int16_t(b[4] | b[5] << 8));
      uint8_t g[6] = {};
      hw::i2cRead(0x6B, 0x3B, g, 6);
      Serial.printf("imuraw gyro %d %d %d\n", int16_t(g[0] | g[1] << 8), int16_t(g[2] | g[3] << 8), int16_t(g[4] | g[5] << 8));
    } else if (!strcmp(line, "neustart-kind")) {   // Spielstände auf Anfang (Einstellungen bleiben), dann Neustart
      static const char* const KEYS[] = {"sterne", "alb_open", "alb_seed", "ci_n", "pet_st", "pet_gp", "pe_play", "pe_calm",
                                         "gr_all", "gr_wk", "gr_wkn", "bal_lvl", "or_ser", "or_best", "or_all", "or_allr", "or_wn", "or_wr"};
      for (const char* k : KEYS) store.prefs.remove(k);
      for (int i = 0; i < 30; i++) { char k[8]; snprintf(k, sizeof k, "ci%d", i); store.prefs.remove(k); }
      for (const char* k : {"pet_n0", "pet_n1", "pet_n2"}) store.prefs.remove(k);
      Serial.println("spielstände zurückgesetzt, starte neu");
      delay(100);
      ESP.restart();
    } else if (!strcmp(line, "log")) { logOn = !logOn; Serial.printf("protokoll %s\n", logOn ? "an" : "aus"); }
    else if (!strcmp(line, "imu")) { imuStream = !imuStream; Serial.printf("imu-stream %s\n", imuStream ? "an" : "aus"); }
    else if (!strncmp(line, "kachel", 6)) app->show(atoi(line + 6));
    else if (!strcmp(line, "bench")) bench();
    else if (!strcmp(line, "prof")) {
      prof = !prof;
      Scene::timer = nullptr;
      if (prof) Scene::timer = []() { return uint32_t(micros()); };
    }
    else if (!strncmp(line, "klopf", 5)) { fakeKnocks = atoi(line + 5) > 0 ? atoi(line + 5) : 1; fakeNext = millis(); }
    else if (const char* p = strstr(line, "\"time\":")) {
      const int64_t t = atoll(p + 7);
      if (t > 1600000000) { epochBase = t - millis() / 1000; Serial.printf("zeit gesetzt %lld\n", (long long)t); }
    }
  }
}

// ---------------- Start ----------------
void setup() {
  Serial.setRxBufferSize(4096);   // Fragenpakete kommen zeilenweise mit bis zu ~700 Byte
  Serial.begin(115200);
  // Liest am Mac niemand mit, läuft der USB-Puffer voll und jedes print blockiert (gemessen: bis 6 s Stillstand).
  // Ohne Timeout gehen überzählige Zeilen einfach verloren.
  Serial.setTxTimeoutMs(0);
  if (!hw::begin()) Serial.println("Display-Init fehlgeschlagen");
  Serial.printf("IMU %s\n", hw::imuOk() ? "ok" : "fehlt");
  const bool audio = hw::audioBegin(Synth::RATE, 70);
  Serial.printf("Audio %s\n", audio ? "ok" : "fehlt");

  store.begin();
  fsOk = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  if (!fsOk) Serial.println("LittleFS fehlt");
  app = new App(synth, store);
  home = new Home(synth, store);
  checkin = new CheckIn(synth, store);
  grummel = new Grummel(synth, store, esp_random(), KID_SKIN);
  breathe = new Breathe(synth, store);
  balance = new Balance(synth, store);
  album = new Album(synth, store, esp_random());
  album->setImages(&images);
  riddle = new Riddle(synth, store, esp_random());
  riddle->setAlbum(album, &images);
  radioPlayer = new (heap_caps_malloc(sizeof(RadioPlayer), MALLOC_CAP_SPIRAM)) RadioPlayer();
  home->setApps(APPS, 7);
  home->setCheckIn(T_CHECKIN);
  checkin->setTargets(T_GRUMMEL, T_BREATHE);
  loadPack();
  loadAlbum();
  app->add(home);
  app->add(checkin);
  app->add(grummel);
  app->add(breathe);
  app->add(balance);
  app->add(album);
  app->add(riddle);
  app->add(new Radio(*radioPlayer));
  app->setHome(T_HOME);
  app->setLockTile(T_RADIO);
  app->setSystemMenu(netMenu());
  app->setSettingsHold(3000);   // Einstellungen nur für Eltern: 3 s halten oder BOOT
  app->setPower(60000, 180000, true, false);   // aus bis zum Tippen, nach 1 min dunkler, nach 3 min schwarz; Klopfen weckt nicht
  app->begin(millis());

  freeQ = xQueueCreate(NBUF, sizeof(uint16_t*));
  fullQ = xQueueCreate(NBUF + 1, sizeof(Band));
  for (int i = 0; i < NBUF; i++) {
    uint16_t* b = static_cast<uint16_t*>(heap_caps_malloc(W * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!b) Serial.println("Zu wenig RAM für die Bänder");
    xQueueSend(freeQ, &b, 0);
  }

  xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 4, nullptr, 0);
  if (audio) xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);
  if (audio) xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 2, nullptr, 0);
  netBegin(radioPlayer);
}

void loop() {
  static uint32_t next = millis();
  static uint32_t lastLog = 0, frames = 0, tickUs = 0, rasterUs = 0, rasterBusyUs = 0;
  static uint32_t lastFrameMs = 0, stalls = 0, worstMs = 0;   // Ruckler seit dem Start
  {
    const uint32_t now = millis(), d = lastFrameMs ? now - lastFrameMs : 0;
    if (d > 50) stalls++;
    if (d > worstMs) worstMs = d;
    lastFrameMs = now;
  }
  static bool blank = false;
  static Input in;

  pollSerial();
  uint32_t t0 = micros();
  portENTER_CRITICAL(&inMux);
  in.nTouch = touchN;
  memcpy(in.touch, touchQ, sizeof(TouchSample) * touchN);
  touchN = 0;
  // höchstens die letzten 64 Werte (bei 30 fps kommen ~17)
  while (accCount > 64) { accHead = (accHead + 1) % 128; accCount--; }
  in.nAcc = accCount;
  for (int i = 0; i < accCount; i++) in.acc[i] = accQ[(accHead + i) % 128];
  accHead = (accHead + accCount) % 128;
  accCount = 0;
  portEXIT_CRITICAL(&inMux);
  // künstliche Schläge: kurz abklingendes Zittern wie auf der Tischplatte
  static uint32_t fakeStart = 0;
  if (fakeKnocks > 0 && millis() >= fakeNext) { fakeStart = millis(); fakeNext += 250; fakeKnocks--; }
  if (fakeStart)
    for (int i = 0; i < in.nAcc; i++) {
      const float u = (in.acc[i].ms - fakeStart) * 0.001f;
      if (in.acc[i].ms >= fakeStart && u < 0.1f) in.acc[i].z += 0.4f * std::exp(-u / 0.012f) * std::sin(6.2831853f * 90 * u + 0.6f);
    }
  in.boot = hw::bootPressed() || fakeBoot > 0;
  if (fakeBoot > 0) fakeBoot--;
  in.voice = voiceLevel;
  in.ms = millis();
  in.epoch = epochBase ? epochBase + in.ms / 1000 : 0;

  app->tick(in);
  portalSync.frame(*app, *radioPlayer, app->currentTitle(), starsGet(store), in.ms);
  {   // Uhrzeit vom Portal (Ortszeit)
    const int64_t pe = radioPlayer->epoch;
    if (pe > 1600000000 && (!epochBase || llabs(epochBase + int64_t(in.ms / 1000) - pe) > 5)) epochBase = pe - in.ms / 1000;
  }
  app->draw(scene);
  uint32_t t1 = micros();
  tickUs += t1 - t0;

  brightness = app->brightness();
  if (app->screenOff()) {
    if (!blank) {   // einmal schwarz schieben, dann Helligkeit 0 und Pause
      scene.reset();
      blank = true;
    } else {
      Band b{nullptr, 0};
      xQueueSend(fullQ, &b, portMAX_DELAY);
      goto pace;
    }
  } else blank = false;

  for (int y = 0; y < H; y += BAND) {
    uint16_t* buf;
    xQueueReceive(freeQ, &buf, portMAX_DELAY);
    const uint32_t tb = micros();
    scene.rasterize(buf, y, BAND, true);   // Panel erwartet Big Endian
    rasterBusyUs += micros() - tb;
    Band b{buf, int16_t(y)};
    xQueueSend(fullQ, &b, portMAX_DELAY);
  }
  rasterUs += micros() - t1;
  frames++;

pace:
  if (imuStream) Serial.printf("imu spitze %.3f ruhe %.3f schläge %d schütteln %.2f\n", app->motion.framePeak, app->motion.noise, app->motion.nKnocks, app->motion.shake);
  if (millis() - lastLog > 2000 && !logOn) { lastLog = millis(); frames = 0; tickUs = 0; rasterUs = 0; rasterBusyUs = 0; imuReads = 0; }
  if (millis() - lastLog > 2000) {
    const float sec = (millis() - lastLog) / 1000.f;
    Serial.printf("fps %.1f  app %lu us  raster %lu us (rechnen %lu)  push %lu us (senden %lu)  formen %d  imu %lu/s (fehler %lu)  heap %u\n", frames / sec,
                  (unsigned long)(frames ? tickUs / frames : 0), (unsigned long)(frames ? rasterUs / frames : 0),
                  (unsigned long)(frames ? rasterBusyUs / frames : 0), (unsigned long)pushUs, (unsigned long)pushBusyUs,
                  scene.size(), (unsigned long)(imuReads / sec), (unsigned long)imuFails, ESP.getFreeHeap());
    Serial.println(app->debug());
    Serial.printf("ruckler seit start: %lu (laengster frame %lu ms)\n", (unsigned long)stalls, (unsigned long)worstMs);
    if (frames && prof) {
      Serial.print("je Formtyp us:");
      for (int k = 0; k < 9; k++) { Serial.printf(" %s %lu", Scene::kindName(k), (unsigned long)(scene.kindUs[k] / frames)); scene.kindUs[k] = 0; }
      Serial.println();
    }
    imuReads = 0;
    lastLog = millis(); frames = 0; tickUs = 0; rasterUs = 0; rasterBusyUs = 0;
  }

  next += 33;
  int32_t wait = int32_t(next - millis());
  if (wait > 0) delay(wait);
  else next = millis();
}
