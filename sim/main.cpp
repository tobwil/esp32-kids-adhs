// Simulator für den Mac (SDL2): dieselbe App wie auf dem Gerät, Beschleunigung wird nachgebildet.
// Maus = Finger (ziehen = wischen, lange drücken = Einstellungen), B = BOOT-Taste, Pfeiltasten = neigen, C = gerade,
// Leertaste = auf den Tisch klopfen, 1/2/3 = leicht/mittel/fest klopfen, T (halten) = Tastatur tippen,
// X (halten) = schütteln, M (halten) = sprechen, P = Bildschirmfoto, Esc = Ende.
//
// Ohne Fenster:
//   dksim --screens <ordner>   Ablauf durchspielen und Bilder als BMP speichern
//   dksim --bench              Rasterzeit pro Frame messen
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "app.h"
#include "grummel.h"
#include "balance.h"
#include "breathe.h"
#include "checkin.h"
#include "home.h"
#include "album.h"
#include "horses.h"
#include "album_builtin.h"
#include "quiz.h"
#include "quiz_kids.h"
#include "rewards.h"
#include "riddle.h"
#include "radio.h"
#include "post.h"
#include "wakeup.h"
#include "portal_net.h"
#include "portal_sync.h"
#include <mutex>
#include <thread>

using namespace dk;

struct FileStore : Store {
  std::string path;
  std::map<std::string, int> kv;
  explicit FileStore(std::string p) : path(std::move(p)) {
    if (FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r")) {
      char k[64]; int v;
      while (fscanf(f, "%63s %d", k, &v) == 2) kv[k] = v;
      fclose(f);
    }
  }
  int get(const char* k, int def) override { auto it = kv.find(k); return it == kv.end() ? def : it->second; }
  void set(const char* k, int v) override {
    kv[k] = v;
    if (path.empty()) return;
    if (FILE* f = fopen(path.c_str(), "w")) { for (auto& [a, b] : kv) fprintf(f, "%s %d\n", a.c_str(), b); fclose(f); }
  }
};

static uint16_t fb[W * H];
static Scene scene;

static void renderAll() {
  for (int y = 0; y < H; y += 16) scene.rasterize(fb + y * W, y, 16);
}

static void saveBmp(const char* path) {
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 24, SDL_PIXELFORMAT_RGB24);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint16_t p = fb[y * W + x];
      uint8_t* d = (uint8_t*)s->pixels + y * s->pitch + x * 3;
      d[0] = uint8_t(((p >> 11) & 31) * 255 / 31); d[1] = uint8_t(((p >> 5) & 63) * 255 / 63); d[2] = uint8_t((p & 31) * 255 / 31);
    }
  SDL_SaveBMP(s, path);
  SDL_FreeSurface(s);
}

// Nachgebildeter Beschleunigungssensor: Gerät liegt flach auf dem Tisch (z = +1 g),
// ein Schlag auf die Tischplatte ist ein kurz abklingendes Zittern.
struct FakeImu {
  struct Hit { uint32_t t0; float amp; };
  std::vector<Hit> hits;
  bool shaking = false;
  float tiltX = 0, tiltY = 0;   // Grad: rechte Kante runter > 0, obere Kante runter > 0
  uint32_t seed = 99, last = 0;
  float rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFF) / 65535.f - 0.5f; }
  void knock(uint32_t ms, float amp) { hits.push_back({ms, amp}); }
  void fill(Input& in, uint32_t ms) {
    in.nAcc = 0;
    if (!last) last = ms - 33;
    for (uint32_t t = last + 2; t <= ms && in.nAcc < 64; t += 2) {
      // Sensor misst die Gegenkraft zur Schwerkraft: rechte Kante runter → x negativ, obere Kante runter → y negativ
      const float rx = tiltX * 0.0174533f, ry = tiltY * 0.0174533f;
      float x = -std::sin(rx) + rnd() * 0.006f, y = -std::sin(ry) + rnd() * 0.006f, z = std::cos(rx) * std::cos(ry) + rnd() * 0.006f;
      for (auto& h : hits) {
        if (t < h.t0) continue;
        const float u = (t - h.t0) * 0.001f;
        if (u > 0.12f) continue;
        const float v = h.amp * std::exp(-u / 0.012f) * std::sin(6.2831853f * 90 * u + 0.6f);
        z += v; x += 0.3f * v;
      }
      if (shaking) { x += 1.8f * std::sin(t * 0.0314f); y += 0.8f * std::sin(t * 0.025f + 1); z += 0.4f * std::sin(t * 0.04f); }
      in.acc[in.nAcc++] = {x, y, z, t};
    }
    last = ms;
    while (!hits.empty() && ms - hits.front().t0 > 200) hits.erase(hits.begin());
  }
};

// ---------------- Ablauf ohne Fenster ----------------

struct Script {
  App& app;
  FakeImu imu;
  Input in;
  uint32_t ms = 1000;
  std::vector<TouchSample> pending;
  const char* dir;
  void frame() {
    in.nTouch = 0;
    for (auto& t : pending) if (in.nTouch < 32) in.touch[in.nTouch++] = t;
    pending.clear();
    imu.fill(in, ms);
    in.ms = ms;
    in.epoch = 1792500000;   // fester Zeitpunkt, damit die Wochenstatistik erscheint
    app.tick(in);
    ms += 33;
  }
  void run(float sec) { for (int i = 0; i < int(sec * 30); i++) frame(); }
  void shot(const char* name) {
    app.draw(scene);
    renderAll();
    char p[512];
    snprintf(p, sizeof p, "%s/%s.bmp", dir, name);
    saveBmp(p);
  }
  void touch(bool down, int x, int y) { pending.push_back({down, int16_t(x), int16_t(y), ms}); }
  void tap(int x, int y) { touch(true, x, y); frame(); touch(false, x, y); frame(); }
};

// Fotos fürs Pferde-Album: Rohdaten aus tools/horses/out (von tools/horses.py fetch)
struct FileImages : ImageStore {
  std::map<int, std::vector<uint16_t>> cards, thumbs;
  static bool read(const std::string& path, size_t n, std::vector<uint16_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(n);
    const bool ok = fread(out.data(), 2, n, f) == n;
    fclose(f);
    return ok;
  }
  Image get(std::map<int, std::vector<uint16_t>>& m, int id, const char* suffix, int w, int h) {
    if (getenv("DK_NOPHOTOS")) return {};   // z. B. für Bilder im öffentlichen Repo
    auto it = m.find(id);
    if (it == m.end()) {
      std::vector<uint16_t> v;
      if (!read(std::string(DK_HORSES) + "/" + std::to_string(id) + suffix, size_t(w) * h, v)) v.clear();
      it = m.emplace(id, std::move(v)).first;
    }
    return it->second.empty() ? Image{} : Image{it->second.data(), w, h};
  }
  Image card(int id) override { return get(cards, id, ".rgb", CARD_W, CARD_H); }
  Image thumb(int id) override { return get(thumbs, id, "_t.rgb", THUMB_W, THUMB_H); }
};

// Portal-Adresse: „host[:port]“ (Mac-Portal 8770, „wrangler dev“ 8787), Schlüssel fürs Cloud-Portal aus DK_SCHLUESSEL
static Endpoint gEp;
static Endpoint makeEndpoint(const char* addr) {
  Endpoint e;
  const char* a = addr && *addr ? addr : "127.0.0.1";
  if (!std::strncmp(a, "https://", 8)) { a += 8; e.tls = true; e.port = 443; }
  else if (!std::strncmp(a, "http://", 7)) a += 7;
  snprintf(e.host, sizeof e.host, "%s", a);
  if (char* c = std::strchr(e.host, ':')) { e.port = std::atoi(c + 1); *c = 0; }
  if (char* sl = std::strchr(e.host, '/')) *sl = 0;
  if (const char* k = getenv("DK_SCHLUESSEL")) snprintf(e.key, sizeof e.key, "%s", k);
  return e;
}

// Kartenbilder eines Portal-Themas: RGB565-Rohdaten vom Portal (der Simulator dekodiert kein JPEG)
struct PortalImages : ImageStore {
  Endpoint ep;
  std::map<int, std::vector<uint16_t>> cards, thumbs;
  static bool sink(void* ctx, const uint8_t* d, int n) { auto* v = static_cast<std::vector<uint8_t>*>(ctx); v->insert(v->end(), d, d + n); return true; }
  Image get(std::map<int, std::vector<uint16_t>>& m, int id, bool thumb) {
    auto it = m.find(id);
    if (it == m.end()) {
      std::vector<uint8_t> raw;
      char path[48];
      snprintf(path, sizeof path, "/album/bild/%d%s.rgb", id, thumb ? "_t" : "");
      const size_t want = size_t(thumb ? THUMB_W * THUMB_H : CARD_W * CARD_H) * 2;
      std::vector<uint16_t> px;
      if (portalDownload(ep, path, sink, &raw) && raw.size() == want) { px.resize(want / 2); std::memcpy(px.data(), raw.data(), want); }
      it = m.emplace(id, std::move(px)).first;
    }
    if (it->second.empty()) return {};
    return {it->second.data(), thumb ? THUMB_W : CARD_W, thumb ? THUMB_H : CARD_H};
  }
  Image card(int id) override { return get(cards, id, false); }
  Image thumb(int id) override { return get(thumbs, id, true); }
  void reload() override { cards.clear(); thumbs.clear(); }
};

// Kinder-Aufbau: 0 Dashboard, 1 Check-in, 2 Grummel, 3 Atmen, 4 Kugel ins Loch, 5 Sammelalbum, 6 Rätsel, 7 Radio, 8 Post, 9 Wecker (versteckt)
enum { T_HOME, T_CHECKIN, T_GRUMMEL, T_BREATHE, T_BALANCE, T_ALBUM, T_RIDDLE, T_RADIO, T_POST, T_WAKE };
static const Home::AppIcon APPS[] = {{"Gefühle", T_CHECKIN, 0}, {"Grummel", T_GRUMMEL, 1}, {"Atmen", T_BREATHE, 2}, {"Kugel", T_BALANCE, 3},
                                     {"Pferde", T_ALBUM, 4},    {"Rätsel", T_RIDDLE, 5},    {"Radio", T_RADIO, 6},  {"Post", T_POST, 7}};
static constexpr int N_APPS = sizeof(APPS) / sizeof(APPS[0]);
static RadioPlayer gRadio;
static Messages gMsgs;
static std::vector<int16_t> gRecBuf(Messages::RATE * Messages::MAX_SECS);

// Gerätemenü im Simulator: nur Zustand, WLAN gibt es hier nicht
struct SimMenu : SystemMenu {
  int count() override { return 2; }
  bool info(int) override { return true; }
  void text(int i, char* b, int n) override {
    if (i == 0) snprintf(b, n, "Simulator: Netz des Macs");
    else snprintf(b, n, "Portal: %s", gRadio.online() ? "verbunden" : "nicht erreichbar");
  }
};
static SimMenu gMenu;

struct Kids {
  Home home;
  CheckIn checkin;
  Grummel grummel;
  Breathe breathe;
  Balance balance;
  Album album;
  Riddle riddle;
  Radio radio{gRadio};
  Post post;
  Wakeup wake;
  QuizPack pack;
  FileImages images;
  Kids(Synth& synth, Store& store, uint32_t seed)
      : home(synth, store), checkin(synth, store), grummel(synth, store, seed, KID_SKIN), breathe(synth, store), balance(synth, store), album(synth, store, seed * 7 + 3),
        riddle(synth, store, seed * 13 + 5), post(synth, gMsgs, gRadio), wake(synth, store, gRadio) {
    gMsgs.setBuffer(gRecBuf.data(), int(gRecBuf.size()));
    gRadio.setMessages(&gMsgs);
    home.setPost(&gMsgs, T_POST);
    album.setImages(&images);
    album.load(ALBUM_BUILTIN);
    pack.load(QUIZ_KIDS);
    riddle.setPack(&pack);
    riddle.setAlbum(&album, &images);
    home.setApps(APPS, N_APPS);
    home.setAlbum(&album);
    home.setCheckIn(T_CHECKIN);
    checkin.setTargets(T_GRUMMEL, T_BREATHE);
  }
  bool startOff = true;
  void addTo(App& app) {
    app.add(&home); app.add(&checkin); app.add(&grummel); app.add(&breathe); app.add(&balance); app.add(&album); app.add(&riddle); app.add(&radio); app.add(&post); app.add(&wake);
    app.setHome(T_HOME);
    app.setHidden(1u << T_WAKE);
    app.setLockTile(T_RADIO);
    app.setSystemMenu(&gMenu);
    app.setSettingsHold(3000);   // Einstellungen nur für Eltern: 3 s halten oder BOOT
    app.setPower(60000, 180000, startOff, false);   // aus bis zum Tippen, nach 3 min wieder schwarz
  }
};

// ---- Textprüfung: Rand und runde Ecken ----
static App* gApp = nullptr;
static std::vector<std::string> gIssues;
static void checkText(const char* str, float x0, float y0, float x1, float y1) {
  char why[64] = "";
  const float CR = 46;   // Eckenradius des Displays (großzügig)
  if (x0 < 8 || x1 > W - 8) snprintf(why, sizeof why, "über den Rand (x %.0f..%.0f)", x0, x1);
  else if (y0 < 2 || y1 > H - 2) snprintf(why, sizeof why, "über den Rand (y %.0f..%.0f)", y0, y1);
  else {
    const float cx[2] = {CR, W - CR}, cy[2] = {CR, H - CR};
    for (int i = 0; i < 2 && !why[0]; i++)
      for (int j = 0; j < 2 && !why[0]; j++) {
        const float px = i ? x1 : x0, py = j ? y1 - 6 : y0 + 6;   // Glyphen füllen die Zeile nicht ganz aus
        const bool inCorner = (i ? px > cx[1] : px < cx[0]) && (j ? py > cy[1] : py < cy[0]);
        if (inCorner && (px - cx[i]) * (px - cx[i]) + (py - cy[j]) * (py - cy[j]) > CR * CR) snprintf(why, sizeof why, "in der runden Ecke");
      }
  }
  if (why[0]) { char b[160]; snprintf(b, sizeof b, "„%s“ %s", str, why); gIssues.push_back(b); }
}

static int screens(const char* dir) {
  Synth synth;
  FileStore store("");
  App app(synth, store);
  Kids kids(synth, store, 7);
  kids.startOff = false;
  kids.addTo(app);
  gApp = &app;
  Scene::textHook = checkText;
  Grummel& grummel = kids.grummel;
  Script sc{app, {}, {}, 1000, {}, dir};
  app.begin(sc.ms);
  int nIssues = 0;
  auto shot = [&](const char* name) {
    gIssues.clear();
    sc.shot(name);
    std::sort(gIssues.begin(), gIssues.end());
    gIssues.erase(std::unique(gIssues.begin(), gIssues.end()), gIssues.end());
    for (auto& i : gIssues) { printf("  %s: %s\n", name, i.c_str()); nIssues++; }
  };
  auto tapFace = [&](int i) { float x, y; CheckIn::faceCenter(i, x, y); sc.tap(int(x), int(y)); };
  // App über das Karussell öffnen: in die Mitte holen, dann antippen
  auto tapIcon = [&](int i) { kids.home.focusApp(APPS[i].tile); sc.run(0.1f); sc.tap(int(W / 2), int(Home::CAR_Y)); };
  // Wischen in 8 Schritten; mid: Bildname für ein Bild mitten im Wisch
  auto swipe = [&](int x0, int y0, int x1, int y1, const char* mid = nullptr) {
    sc.touch(true, x0, y0); sc.frame();
    for (int k = 1; k <= 8; k++) { sc.touch(true, x0 + (x1 - x0) * k / 8, y0 + (y1 - y0) * k / 8); sc.frame(); if (k == 5 && mid) shot(mid); }
    sc.touch(false, x1, y1); sc.frame(); sc.run(0.5f);
  };
  auto close = [&]() { swipe(4, 250, 220, 250); };   // vom Rand zur Seite wischen
  int guard = 0;

  // Ei: streicheln, bis Grummel schlüpft
  sc.run(1.0f); shot("00_ei");
  sc.touch(true, 150, 166); sc.frame();
  for (int k = 0; k < 400 && kids.home.pet().stage == Pet::EGG; k++) {
    sc.touch(true, 150 + int(70 * std::fabs(std::sin(k * 0.2f))), 166); sc.frame();
    if (k == 60) shot("00a_ei_risse");
  }
  sc.touch(false, 184, 166); sc.frame();
  sc.run(0.4f); shot("00b_geschluepft");
  sc.run(3.5f); shot("01_dashboard_frage");
  // füttern: Apfel antippen
  { float x, y; Home::needPos(0, x, y); sc.tap(int(x), int(y)); }
  sc.run(0.35f); shot("01a_apfel_fliegt");
  sc.run(0.6f); shot("01b_kauen");
  sc.run(2.0f);
  // streicheln: Finger hin und her über Grummel
  sc.touch(true, 140, 150); sc.frame();
  for (int k = 0; k < 40; k++) { sc.touch(true, 140 + int(80 * std::fabs(std::sin(k * 0.25f))), 150 + (k % 7)); sc.frame(); }
  shot("02_streicheln");
  sc.touch(false, 180, 150); sc.frame(); sc.run(1.5f);
  sc.tap(int(Home::BUDDY_X), int(kids.home.buddyY())); sc.run(0.4f); shot("03_kichern");
  sc.run(1.5f);
  sc.imu.shaking = true; sc.run(1.0f); shot("04_schwindelig"); sc.imu.shaking = false; sc.run(2.0f);
  sc.in.voice = 0.5f; sc.run(1.2f); shot("05_zuhoeren"); sc.in.voice = 0; sc.run(0.4f); shot("05b_zugehoert"); sc.run(2.5f);
  // Wie geht's dir? → wütend → Grummel → satt → atmen → zurück zum Check-in → froh → Dashboard
  tapIcon(0); sc.run(0.8f); shot("06_checkin");
  tapFace(CheckIn::WUT); sc.run(4.0f); shot("07_grummel");
  static const int TX[6] = {90, 280, 120, 250, 184, 70}, TY[6] = {130, 140, 340, 330, 100, 240};
  int taps = 0;
  while (grummel.phase() != Grummel::FULL && guard++ < 120) { sc.tap(TX[taps % 6], TY[taps % 6]); taps++; sc.run(0.15f); if (taps == 8) shot("08_grummel_laden"); }
  sc.run(1.1f); shot("09_boerps");
  while (grummel.phase() != Grummel::BREATHE && guard++ < 900) sc.run(0.1f);
  sc.run(2.8f); shot("10_grummel_atmen");
  while (grummel.phase() != Grummel::THANKS && guard++ < 2000) sc.run(0.1f);
  sc.run(1.2f); shot("11_lecker");
  sc.run(5.0f); shot("12_checkin_jetzt");
  tapFace(CheckIn::FROH); sc.run(1.5f); shot("13_froh");
  sc.run(4.0f); shot("14_dashboard_zurueck");
  // Atmen öffnen und zur Seite wegwischen
  tapIcon(2); sc.run(4.0f); shot("15_atmen");
  close(); shot("16_dashboard_nach_x");
  // Wischen: nach oben blättert durch die Apps, zur Seite schließt
  // Karussell: zur Seite wischen dreht, die Mitte öffnet
  kids.home.focusApp(T_CHECKIN); sc.run(0.3f); shot("16_karussell");
  swipe(260, 330, 120, 330, "16_karussell_wisch"); sc.run(0.5f);
  if (kids.home.focusedTile() != T_GRUMMEL) printf("  FEHLER: Karussell dreht nicht (Mitte %d)\n", kids.home.focusedTile()), nIssues++;
  sc.tap(int(W - 40), int(Home::CAR_Y)); sc.run(0.5f);
  if (kids.home.focusedTile() != T_BREATHE) printf("  FEHLER: Nachbar antippen holt ihn nicht in die Mitte (%d)\n", kids.home.focusedTile()), nIssues++;
  if (app.current() != T_HOME) printf("  FEHLER: Karussell öffnet beim Drehen eine App (%d)\n", app.current()), nIssues++;
  kids.home.focusApp(T_CHECKIN); sc.run(0.3f);
  swipe(334, 250, 334, 100, "16a_wisch_hoch"); shot("16b_gefuehle");
  if (app.current() != T_CHECKIN) printf("  FEHLER: Wisch hoch auf dem Dashboard öffnet nicht Gefühle (%d)\n", app.current()), nIssues++;
  swipe(334, 250, 334, 100); if (app.current() != T_GRUMMEL) printf("  FEHLER: Wisch hoch → Grummel (%d)\n", app.current()), nIssues++;
  swipe(300, 250, 300, 400); if (app.current() != T_CHECKIN) printf("  FEHLER: Wisch runter → Gefühle (%d)\n", app.current()), nIssues++;
  swipe(300, 250, 300, 400); if (app.current() != T_HOME) printf("  FEHLER: Wisch runter → Dashboard (%d)\n", app.current()), nIssues++;
  tapIcon(5); sc.run(0.6f); swipe(300, 150, 300, 300); if (app.current() != T_ALBUM) printf("  FEHLER: Rätsel, Wisch runter → Album (%d)\n", app.current()), nIssues++;
  swipe(80, 250, 280, 250, "16c_wisch_zu"); if (app.current() != T_HOME) printf("  FEHLER: Wisch zur Seite schließt nicht (%d)\n", app.current()), nIssues++;
  sc.run(0.5f); shot("16d_dashboard_nach_wisch");
  tapIcon(3); sc.run(0.6f); swipe(184, 300, 184, 150); if (app.current() != T_BALANCE) printf("  FEHLER: Kugel: Fingerjoystick darf nicht blättern (%d)\n", app.current()), nIssues++;
  swipe(4, 250, 200, 250); if (app.current() != T_HOME) printf("  FEHLER: Kugel: Wisch vom Rand schließt nicht (%d)\n", app.current()), nIssues++;
  sc.run(0.5f);
  // Kugel ins Loch
  tapIcon(3); sc.run(0.6f); shot("17_kugel_start");
  sc.run(1.0f); sc.touch(true, 184, 400); sc.run(0.8f); shot("18_kugel_finger"); sc.touch(false, 184, 400); sc.frame();
  close();
  // Hintergründe der ersten vier Runden
  for (int l = 0; l < 4; l++) { app.show(T_BALANCE); kids.balance.setLevel(l); sc.run(1.6f); char n[32]; snprintf(n, sizeof n, "18%c_kugel_runde%d", 'a' + l, l + 1); shot(n); }
  app.show(T_HOME); sc.run(0.5f);
  // Pferde-Album: 9 Sterne → Abzeichen, Karten aufdecken, blättern
  while (starsGet(store) < 9) starsAdd(store);
  sc.run(0.5f); shot("19_dashboard_karte_wartet");
  tapIcon(4); sc.run(1.0f); shot("20_verdeckt");
  sc.tap(184, 214); sc.run(0.6f); shot("21_aufgedeckt");
  sc.run(1.5f); sc.tap(184, 214); sc.run(0.6f); shot("22_rueckseite");
  for (int k = 0; k < 12 && kids.album.phase() != Album::GRID; k++) { sc.tap(184, 214); sc.run(1.2f); }
  sc.run(0.8f); shot("23_album");
  sc.tap(W - 70, 404); sc.run(0.6f); shot("24_album_seite2");
  // alle Karten einmal ansehen (Textprüfung auf Vorder- und Rückseite)
  for (int i = 0; i < kids.album.count(); i++) { while (starsGet(store) < 3 * (i + 1)) starsAdd(store); }
  app.show(T_ALBUM);
  for (int k = 0; k < 90 && kids.album.phase() != Album::GRID; k++) {
    sc.tap(184, 214); sc.run(0.7f);
    char n[32]; snprintf(n, sizeof n, "z%02d_karte", k); if (k % 2 == 0) shot(n);
  }
  close();
  // Rätsel: drei Runden, abwechselnd richtig und falsch antworten (Textprüfung für viele Fragen)
  for (int round = 0, q = 0; round < 3; round++) {
    tapIcon(5); sc.run(0.8f);
    for (int k = 0; k < 8 && kids.riddle.phase() != Riddle::DONE; k++, q++) {
      char n[40];
      snprintf(n, sizeof n, "r%d%d_frage", round, k); if (round == 0 || kids.riddle.kind() != Riddle::TEXT) shot(n);
      if (kids.riddle.kind() == Riddle::COUNT) {   // Zählhilfe: zwei Dinge antippen
        float x, y; kids.riddle.countPos(0, x, y); sc.tap(x, y); sc.run(0.3f);
        kids.riddle.countPos(1, x, y); sc.tap(x, y); sc.run(0.4f);
        snprintf(n, sizeof n, "r%d%d_gezaehlt", round, k); shot(n);
      }
      const int pick = q % 3 == 1 ? (kids.riddle.correct() + 1) % 3 : kids.riddle.correct();
      float x, y, w, h; kids.riddle.answerRect(pick, x, y, w, h);
      sc.tap(x + w / 2, y + h / 2); sc.run(q % 3 == 1 ? 3.5f : 0.5f);
      snprintf(n, sizeof n, "r%d%d_antwort", round, k); if (round == 0 || kids.riddle.kind() != Riddle::TEXT) shot(n);
      if (kids.riddle.phase() == Riddle::WRONG) { sc.tap(184, 300); sc.run(0.5f); }
      else sc.run(1.4f);
    }
    sc.run(0.8f); if (round == 0) shot("r0_geschafft");
    sc.run(6.0f);
  }
  // Radio: ohne Portal, dann mit Sendern vom Portal (Antwort nachgestellt), abspielen, Sperre
  tapIcon(6); sc.run(0.8f); shot("27_radio_offline");
  gRadio.parse("zeit 1792500000\nsperre 0 1170 420\nlaut 70\nschlaf 30\nquelle a1 aufnahme Gute-Nacht-Geschichte von Papa\n"
               "quelle sp-x spotify Kinderlieder zum Einschlafen\nquelle a2 aufnahme Pferdegeschichte\n");
  gRadio.setOnline(true);
  sc.run(0.5f); shot("28_radio_sender");
  sc.tap(int(Radio::PLAY_X), int(Radio::PLAY_Y)); sc.run(0.6f); shot("29_radio_laedt");
  { int16_t z[4000] = {}; for (int i = 0; i < 4000; i++) z[i] = int16_t(8000 * std::sin(i * 0.05f)); gRadio.ring.write(z, 4000); int16_t m[4000] = {}; gRadio.mix(m, 2000); }
  sc.tap(int(Radio::NEXT_X), int(Radio::PLAY_Y)); sc.run(0.4f);
  { int16_t z[4000]; for (int i = 0; i < 4000; i++) z[i] = int16_t(8000 * std::sin(i * 0.05f)); gRadio.ring.write(z, 4000); }
  sc.run(0.3f); shot("30_radio_spotify");
  { char a[96]; std::vector<std::string> acts; while (gRadio.takeAction(a, sizeof a)) acts.push_back(a);
    if (acts.size() < 2 || acts.back().find("aktion=play&quelle=sp-x") == std::string::npos) printf("  FEHLER: Radio-Befehle ans Portal fehlen (%zu)\n", acts.size()), nIssues++; }
  sc.tap(int(Radio::PLAY_X), int(Radio::PLAY_Y)); sc.run(0.3f);
  if (gRadio.playing()) printf("  FEHLER: Radio stoppt nicht\n"), nIssues++;
  {   // mehrteiliger Sender (Hörspiel-CD): Teile nacheinander, „Nächster Teil“, nach dem letzten Ende
    gRadio.parse("zeit 1792500000\nsperre 0 1170 420\nlaut 70\nschlaf 0\nquelle a5 aufnahme Bibi und Tina Folge 3\nteile a5 3\n");
    gRadio.select(0); gRadio.play();
    char path[64]; std::vector<std::string> seen;
    gRadio.wantedStream(path, sizeof path); seen.push_back(path);
    gRadio.streamEnded(gRadio.wantedStream(path, sizeof path)); gRadio.wantedStream(path, sizeof path); seen.push_back(path);
    gRadio.skip(); gRadio.wantedStream(path, sizeof path); seen.push_back(path);
    sc.run(0.3f); shot("30b_radio_hoerspiel");
    gRadio.streamEnded(gRadio.wantedStream(path, sizeof path));
    const bool ok = seen[0] == "/radio/a5_1.pcm" && seen[1] == "/radio/a5_2.pcm" && seen[2] == "/radio/a5_3.pcm" && !gRadio.playing();
    if (!ok) printf("  FEHLER: Hörspiel-Teile: %s %s %s, spielt %d\n", seen[0].c_str(), seen[1].c_str(), seen[2].c_str(), gRadio.playing()), nIssues++;
    char a[96]; while (gRadio.takeAction(a, sizeof a)) {}
  }
  close(); sc.run(0.5f);
  gRadio.parse("zeit 1792500000\nsperre 1 1170 420\nlaut 70\nschlaf 30\nquelle a1 aufnahme Gute-Nacht-Geschichte von Papa\n");
  { PortalSync ps; ps.frame(app, gRadio, "x", 0, sc.ms); }
  sc.run(1.0f); shot("31_sperre_radio");
  if (app.current() != T_RADIO) printf("  FEHLER: Sperre zeigt nicht das Radio (%d)\n", app.current()), nIssues++;
  swipe(4, 250, 220, 250); swipe(334, 250, 334, 100);
  if (app.current() != T_RADIO) printf("  FEHLER: Sperre lässt sich wegwischen (%d)\n", app.current()), nIssues++;
  sc.touch(true, 30, 230); sc.run(3.4f); if (app.settingsOpen()) printf("  FEHLER: Sperre: Einstellungen schon nach 3 s\n"), nIssues++;
  sc.run(5.0f); sc.touch(false, 30, 230); sc.frame(); sc.run(0.5f); shot("32_sperre_einstellungen");
  if (!app.settingsOpen()) printf("  FEHLER: Sperre: Einstellungen nach 8 s fehlen\n"), nIssues++;
  app.setLock(0, 1170, 420); sc.tap(184, 30); sc.run(1.0f);
  if (app.current() != T_HOME) printf("  FEHLER: nach Entsperren nicht aufs Dashboard (%d)\n", app.current()), nIssues++;
  // eigenes Kartenthema (Format 2, ohne Bilder) und eine abgeschaltete App
  {
    static const char DINOS[] = "#album dinos\n#titel Dino-Album\n#symbol Dinos\n#zaehlwort Dinos\n#frage Welcher Dino ist das?\n#format 2\n"
        "1|Triceratops|Pflanzenfresser|Kreidezeit, Nordamerika|Länge: etwa 9 m|Nahrung|Farne und Palmfarne|Lebte vor|68 Millionen Jahren|Seine drei Hörner halfen ihm bei der Verteidigung.|Foto: eigenes Bild\n"
        "2|Brachiosaurus|Pflanzenfresser|Jurazeit, Nordamerika|Höhe: etwa 13 m|Nahrung|Blätter hoher Bäume|Lebte vor|150 Millionen Jahren|Er war so hoch wie ein Haus mit vier Stockwerken.|\n"
        "3|T-Rex|Fleischfresser|Kreidezeit|Länge: etwa 12 m|Nahrung|andere Dinosaurier|Lebte vor|67 Millionen Jahren||\n";
    kids.album.setImages(nullptr);   // Thema ohne Bilder: Platzhalter
    kids.album.load(DINOS);
    if (kids.album.count() != 3 || kids.album.pending() != 0) printf("  FEHLER: neues Thema: %d Karten, %d wartend (soll 3, 0)\n", kids.album.count(), kids.album.pending()), nIssues++;
    for (int k = 0; k < 3; k++) starsAdd(store);
    app.setDisabled(1u << T_BALANCE);
    app.show(T_HOME); sc.run(0.6f); shot("33_dashboard_dinos_ohne_kugel");
    if (kids.album.pending() != 1) printf("  FEHLER: nach 3 Sternen keine Dino-Karte\n"), nIssues++;
    for (int k = 0; k < 4; k++) swipe(334, 250, 334, 100);   // Gefühle, Grummel, Atmen, (Kugel ist aus) Album
    if (app.current() != T_ALBUM) printf("  FEHLER: Wischen überspringt abgeschaltete App nicht (%d)\n", app.current()), nIssues++;
    app.show(T_ALBUM); sc.run(0.6f); sc.tap(184, 214); sc.run(1.2f); shot("34_dino_vorne");
    sc.tap(184, 214); sc.run(0.6f); shot("35_dino_hinten");
    sc.tap(184, 214); sc.run(0.6f); shot("36_dino_album");
    app.setDisabled(0);
    kids.album.setImages(&kids.images);
    kids.album.load(ALBUM_BUILTIN);
    close();
  }
  // Post: Nachricht von Papa, Kontakte, aufnehmen und senden
  gRadio.parse("zeit 1792500000\nsperre 0 1170 420\nlaut 70\nschlaf 30\nquelle a1 aufnahme Gute-Nacht-Geschichte\n"
               "nachricht n2 0 Papa\nnachricht n1 1 Oma Gisela\nkontakt k1 Mama\nkontakt k2 Papa\nkontakt k3 Oma Gisela\n");
  app.show(T_HOME); sc.run(1.0f); shot("37_post_von_papa");
  tapIcon(7); sc.run(0.6f); shot("38_post_liste");
  { float x, y, w, h; Post::rowRect(0, x, y, w, h); sc.tap(int(x + w / 2), int(y + h / 2)); sc.run(0.3f); }
  if (!gRadio.clipPlaying() || gMsgs.unheard() != 0) printf("  FEHLER: Nachricht spielt nicht oder bleibt ungehört\n"), nIssues++;
  { char a[96]; bool heard = false; while (gMsgs.takeAction(a, sizeof a)) heard |= std::strstr(a, "gehoert?id=n2") != nullptr;
    if (!heard) printf("  FEHLER: „gehört“ geht nicht ans Portal\n"), nIssues++; }
  shot("39_post_spielt");
  gRadio.stopClip();
  { float x, y; Post::contactPos(1, 3, x, y); sc.tap(int(x), int(y)); sc.run(0.6f); shot("40_post_an_papa"); }
  sc.tap(int(Post::REC_X), int(Post::REC_Y)); sc.run(0.2f);
  { int16_t b[16000]; for (int i = 0; i < 16000; i++) b[i] = int16_t(3000 * std::sin(i * 0.05f)); for (int k = 0; k < 3; k++) { gMsgs.feed(b, 16000); sc.in.voice = 0.6f; sc.run(0.3f); } }
  shot("41_post_nimmt_auf");
  sc.in.voice = 0;
  sc.tap(int(Post::REC_X), int(Post::REC_Y)); sc.run(0.4f); shot("42_post_fertig");
  sc.tap(int(Post::REC_X + Post::ACT_DX), int(Post::ACT_Y)); sc.run(0.8f); shot("43_post_gesendet");
  { char p[96]; const int16_t* d; int n = 0;
    if (!gMsgs.takeUpload(p, sizeof p, &d, &n) || !std::strstr(p, "an=k2") || n < 40000) printf("  FEHLER: Aufnahme nicht zum Senden bereit (%d Samples)\n", n), nIssues++;
    else gMsgs.uploadDone(true); }
  sc.run(3.0f);
  close();
  // Wecker kommt auch durch die Sperre, danach zurück zum Radio
  app.setLock(1, 1170, 420); sc.run(1.0f);
  app.alert(T_WAKE); sc.run(12.0f); shot("44_wecker");
  if (app.current() != T_WAKE) printf("  FEHLER: Wecker erscheint nicht (%d)\n", app.current()), nIssues++;
  sc.tap(184, 300); sc.run(1.0f);
  if (app.current() != T_RADIO) printf("  FEHLER: nach dem Wecker nicht zurück zum Radio (%d)\n", app.current()), nIssues++;
  // Nachtlicht: Schlafenszeit ganztags, Bildschirm geht aus → dunkle Uhr
  gRadio.nightLight = true;
  { PortalSync ps; ps.frame(app, gRadio, "x", 0, sc.ms); }   // übernimmt auch die Sperrzeiten vom „Portal“, deshalb danach:
  app.setLock(0, 0, 1439);
  sc.run(190.0f); shot("45_nachtlicht");
  if (app.screenOff() || app.brightness() > 20) printf("  FEHLER: Nachtlicht fehlt (aus %d, Helligkeit %d)\n", app.screenOff(), app.brightness()), nIssues++;
  gRadio.nightLight = false;
  { PortalSync ps; ps.frame(app, gRadio, "x", 0, sc.ms); }
  app.setLock(0, 1170, 420);
  sc.tap(184, 300); sc.run(1.0f);
  // Einstellungen: 3 s auf eine freie Stelle drücken
  sc.touch(true, 30, 230); sc.run(3.4f); sc.touch(false, 30, 230); sc.frame(); sc.run(0.5f); shot("25_einstellungen");
  swipe(184, 380, 184, 120); sc.run(0.3f); shot("25b_einstellungen_unten");
  sc.tap(184, 20); sc.run(0.5f);
  tapIcon(3); sc.run(0.5f); sc.touch(true, 4, 230); sc.run(3.4f); sc.touch(false, 4, 230); sc.frame(); sc.run(0.5f); shot("25c_einstellungen_kugel");
  sc.tap(184, 20); sc.run(0.4f); close();
  // gewachsen mit Krone (viel Pflege, 50 Sterne)
  store.set("pet_st", Pet::BIG); store.set("pet_gp", 60);
  while (starsGet(store) < 50) starsAdd(store);
  { Kids k2(synth, store, 9); App a2(synth, store); k2.startOff = false; k2.addTo(a2); Script s2{a2, {}, {}, 1000, {}, dir}; a2.begin(s2.ms); s2.run(2.5f); gIssues.clear(); s2.shot("26_gross_krone"); }
  printf("%d Textverstöße\n", nIssues);
  printf("Bilder in %s\n", dir);
  return 0;
}

static int bench() {
  Synth synth;
  FileStore store("");
  App app(synth, store);
  Kids kids(synth, store, 3);
  kids.startOff = false;
  kids.addTo(app);
  Script sc{app, {}, {}, 1000, {}, "."};
  app.begin(sc.ms);
  app.show(T_GRUMMEL);
  for (int i = 0; i < 10; i++) { sc.imu.knock(sc.ms, 0.4f); sc.run(0.25f); }
  Uint64 f = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
  int n = 300, cmds = 0;
  for (int i = 0; i < n; i++) { sc.frame(); app.draw(scene); cmds += scene.size(); renderAll(); }
  double ms = double(SDL_GetPerformanceCounter() - t0) / f * 1000 / n;
  printf("%.2f ms pro Frame (Mac), %d Formen im Schnitt\n", ms, cmds / n);
  return 0;
}

// Ende-zu-Ende mit dem laufenden Portal: Sender holen, ersten abspielen, Ton messen
static int portalTest(const char* host) {
  gMsgs.setBuffer(gRecBuf.data(), int(gRecBuf.size()));
  gRadio.setMessages(&gMsgs);
  gEp = makeEndpoint(host);
  if (!portalPoll(gRadio, &gMsgs, gEp)) { printf("Portal %s nicht erreichbar\n", host); return 1; }
  const int n = gRadio.count();
  printf("Portal erreichbar, %d Sender\n", n);
  for (int i = 0; i < n; i++) { RadioSource s; gRadio.source(i, s); printf("  %s  %s  %s\n", s.id, s.spotify ? "spotify" : "aufnahme", s.name); }
  std::thread([] { portalStreamLoop(gRadio, [] { return gEp; }); }).detach();
  int16_t buf[1600];
  double sum = 0;
  long cnt = 0;
  if (n) {
    gRadio.play();
    for (int k = 0; k < 30; k++) {   // 3 s in Echtzeit
      gRadio.tick(0.1f);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::memset(buf, 0, sizeof buf);
      gRadio.mix(buf, 1600);
      for (int16_t v : buf) { sum += double(v) * v; cnt++; }
    }
  }
  portalPoll(gRadio, &gMsgs, gEp);   // schickt auch die Befehle (play) ans Portal
  printf("Album-Thema: %s (Version %08x)\n", gRadio.albumTheme, unsigned(gRadio.albumVersion));
  printf("Wecker %s %d:%02d Tage %02x Ton %s, Nachtlicht %s, %d Dateien für die SD-Karte\n", gRadio.alarmOn ? "an" : "aus", gRadio.alarmMin / 60, gRadio.alarmMin % 60,
         gRadio.alarmDays, gRadio.alarmTone, gRadio.nightLight ? "an" : "aus", gRadio.nFiles);
  printf("Post: %d Nachrichten (%d neu), %d Kontakte\n", gMsgs.count(), gMsgs.unheard(), gMsgs.contacts());
  {   // Rätsel-Fragen aus dem Portal
    std::vector<uint8_t> raw;
    if (gRadio.quizVersion && portalDownload(gEp, "/fragen.txt", PortalImages::sink, &raw)) {
      raw.push_back(0);
      QuizPack qp;
      qp.load(reinterpret_cast<char*>(raw.data()));
      Question q;
      printf("Fragen (Version %08x): %d, %d Kategorien, erste: „%s“\n", unsigned(gRadio.quizVersion), qp.count(), qp.categories(), qp.get(0, q) ? q.q : "-");
    }
  }
  Message msg;
  Contact con;
  if (gMsgs.newest(msg)) {   // anhören: Clip läuft über den Tonstrom
    char path[48];
    snprintf(path, sizeof path, "/nachricht/%s.pcm", msg.id);
    gRadio.stop();
    gRadio.playClip(path);
    gMsgs.markHeard(msg.id);
    double sum = 0; long cnt = 0;
    for (int k = 0; k < 15; k++) { gRadio.tick(0.1f); std::this_thread::sleep_for(std::chrono::milliseconds(100)); std::memset(buf, 0, sizeof buf); gRadio.mix(buf, 1600); for (int16_t v : buf) { sum += double(v) * v; cnt++; } }
    printf("  Nachricht von %s angehört, Pegel %.0f\n", msg.from, std::sqrt(sum / std::max(1L, cnt)));
  }
  if (gMsgs.contact(0, con)) {   // antworten: 1,5 s Ton aufnehmen und senden
    gMsgs.startRecording();
    int16_t b[24000];
    for (int i = 0; i < 24000; i++) b[i] = int16_t(5000 * std::sin(i * 0.03f));
    gMsgs.feed(b, 24000);
    gMsgs.stopRecording();
    gMsgs.send(con.id);
    portalPoll(gRadio, &gMsgs, gEp);
    printf("  Antwort an %s %s\n", con.name, gMsgs.sending() ? "NICHT gesendet" : "gesendet");
  }
  if (std::strcmp(gRadio.albumTheme, "pferde") != 0) {
    std::vector<uint8_t> raw;
    if (portalDownload(gEp, "/album/karten.txt", PortalImages::sink, &raw)) {
      raw.push_back(0);
      Synth synth; FileStore store("");
      Album a(synth, store, 1);
      PortalImages pi; pi.ep = gEp;
      a.load(reinterpret_cast<char*>(raw.data()));
      printf("  %s: %d Karten, Dashboard „%s“, Frage „%s“\n", a.title(), a.count(), a.shortName(), a.question());
      int withImg = 0;
      for (int i = 0; i < a.count(); i++) withImg += pi.card(a.cardInfo(i).id).px != nullptr;
      printf("  Bilder: %d von %d\n", withImg, a.count());
    }
  }
  printf("Ton empfangen: Pegel (RMS) %.0f, Puffer %d Samples, %s\n", std::sqrt(sum / std::max(1L, cnt)), gRadio.ring.fill(), gRadio.playing() ? "spielt" : "gestoppt");
  return 0;
}

// gezeichnete Pferde als Kartenbilder (328×196) für Vorführungen, ohne fremde Fotos
static int horseImages(const char* dir) {
  static const Horse H[4] = {{"", 150, Coat::CHESTNUT, 0xc07a3a, 0xf0dca0, false}, {"", 160, Coat::BLACK, 0x2a2420, 0x15110f, true},
                             {"", 150, Coat::GREY, 0xe8e4dc, 0xc8c2b8, false}, {"", 140, Coat::PINTO, 0x8a5a3a, 0x3a2418, false}};
  static const uint32_t SKY[4] = {0x9cc8f0, 0xf0c89c, 0xb8d8f0, 0xc8b8f0};
  for (int i = 0; i < 4; i++) {
    scene.reset();
    scene.ellipse(164, 60, 420, 200, hex(SKY[i]), hex(0xf6f0e0));
    scene.ellipse(164, 260, 360, 110, hex(0x7cc06a), hex(0x4a9a4a));
    scene.circle(270, 50, 22, hex(0xfff0b0));
    drawHorse(scene, H[i], 150, 170, 1.15f, 0.3f);
    renderAll();
    char p[512];
    snprintf(p, sizeof p, "%s/pferd%d.bmp", dir, i + 1);
    saveBmp(p);
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 2 && !strcmp(argv[1], "--screens")) return screens(argv[2]);
  if (argc > 2 && !strcmp(argv[1], "--pferdebilder")) return horseImages(argv[2]);
  if (argc > 1 && !strcmp(argv[1], "--portal-test")) return portalTest(argc > 2 ? argv[2] : "127.0.0.1");
  if (argc > 1 && !strcmp(argv[1], "--bench")) return bench();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_Window* win = SDL_CreateWindow("Desk-Kacheln", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren, W, H);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);

  Synth synth;
  SDL_AudioSpec want{}, have{};
  want.freq = Synth::RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
  want.callback = [](void* u, Uint8* stream, int len) {
    static_cast<Synth*>(u)->render((int16_t*)stream, len / 2);
    gRadio.mix((int16_t*)stream, len / 2);
  };
  want.userdata = &synth;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);

  std::string home = getenv("HOME") ? getenv("HOME") : ".";
  FileStore store(home + "/.desk-kacheln.txt");
  App app(synth, store);
  Kids kids(synth, store, SDL_GetTicks() * 2654435761u + 1);
  kids.addTo(app);
  app.begin(SDL_GetTicks());

  // Eltern-Portal (tools/portal): Adresse aus DK_PORTAL, sonst der Mac selbst
  gEp = makeEndpoint(getenv("DK_PORTAL"));
  std::thread([] { for (;;) { portalPoll(gRadio, &gMsgs, gEp); std::this_thread::sleep_for(std::chrono::seconds(2)); } }).detach();
  std::thread([] { portalStreamLoop(gRadio, [] { return gEp; }); }).detach();
  PortalSync sync;
  static PortalImages portalImages;
  portalImages.ep = gEp;
  static std::mutex albumMu;
  static std::string albumText, albumKey;   // vom Netz-Thread geholt, im Hauptthread geladen
  static bool albumNew = false;
  static std::string quizText;   // Rätsel-Fragen vom Portal
  static bool quizNew = false;
  std::thread([] {
    std::string have;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      static uint32_t quizHave = 0;
      std::vector<uint8_t> qraw;
      if (gRadio.quizVersion && gRadio.quizVersion != quizHave && portalDownload(gEp, "/fragen.txt", PortalImages::sink, &qraw)) {
        qraw.push_back(0);
        std::lock_guard<std::mutex> g(albumMu);
        quizText = reinterpret_cast<char*>(qraw.data());
        quizNew = true;
        quizHave = gRadio.quizVersion;
      }
      if (!gRadio.albumTheme[0]) continue;
      char key[48];
      snprintf(key, sizeof key, "%s %08x", gRadio.albumTheme, unsigned(gRadio.albumVersion));
      if (have == key) continue;
      std::vector<uint8_t> raw;
      const bool builtin = !std::strcmp(gRadio.albumTheme, "pferde");
      if (!builtin && !portalDownload(gEp, "/album/karten.txt", PortalImages::sink, &raw)) continue;
      raw.push_back(0);
      std::lock_guard<std::mutex> g(albumMu);
      albumText = builtin ? std::string(ALBUM_BUILTIN) : std::string(reinterpret_cast<char*>(raw.data()));
      albumKey = key;
      albumNew = true;
      have = key;
    }
  }).detach();

  FakeImu imu;
  bool running = true, mouseDown = false;
  int shotN = 0;
  uint32_t lastFrame = SDL_GetTicks(), lastLog = 0, typeNext = 0;
  Input in;
  std::vector<TouchSample> touches;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      const uint32_t now = SDL_GetTicks();
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        switch (e.key.keysym.sym) {
          case SDLK_ESCAPE: running = false; break;
          case SDLK_SPACE: imu.knock(now, 0.3f); break;
          case SDLK_1: imu.knock(now, 0.12f); break;
          case SDLK_2: imu.knock(now, 0.3f); break;
          case SDLK_3: imu.knock(now, 0.8f); break;
          case SDLK_p: {
            char p[64]; snprintf(p, sizeof p, "screenshot_%02d.bmp", shotN++);
            app.draw(scene); renderAll(); saveBmp(p); printf("%s\n", p);
            break;
          }
          default: break;
        }
      }
      if (e.type == SDL_MOUSEBUTTONDOWN) { mouseDown = true; touches.push_back({true, int16_t(e.button.x), int16_t(e.button.y), now}); }
      if (e.type == SDL_MOUSEMOTION && mouseDown) touches.push_back({true, int16_t(e.motion.x), int16_t(e.motion.y), now});
      if (e.type == SDL_MOUSEBUTTONUP) { mouseDown = false; touches.push_back({false, int16_t(e.button.x), int16_t(e.button.y), now}); }
    }
    const uint32_t now = SDL_GetTicks();
    if (now - lastFrame < 33) { SDL_Delay(2); continue; }
    lastFrame = now;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    imu.shaking = k[SDL_SCANCODE_X];
    // Pfeiltasten neigen das Gerät (für Kugel ins Loch), C = gerade
    if (k[SDL_SCANCODE_RIGHT]) imu.tiltX = std::fmin(20.f, imu.tiltX + 1.2f);
    if (k[SDL_SCANCODE_LEFT]) imu.tiltX = std::fmax(-20.f, imu.tiltX - 1.2f);
    if (k[SDL_SCANCODE_UP]) imu.tiltY = std::fmin(20.f, imu.tiltY + 1.2f);
    if (k[SDL_SCANCODE_DOWN]) imu.tiltY = std::fmax(-20.f, imu.tiltY - 1.2f);
    if (k[SDL_SCANCODE_C]) imu.tiltX = imu.tiltY = 0;
    in.voice = k[SDL_SCANCODE_M] ? 0.5f : 0.f;   // M halten = sprechen
    if (k[SDL_SCANCODE_T] && now >= typeNext) { imu.knock(now, 0.02f + 0.02f * (now % 7) / 7.f); typeNext = now + 90 + now % 60; }
    in.nTouch = 0;
    for (auto& t : touches) if (in.nTouch < 32) in.touch[in.nTouch++] = t;
    touches.clear();
    imu.fill(in, now);
    in.boot = k[SDL_SCANCODE_B];
    in.ms = now;
    time_t tt = time(nullptr);
    struct tm lt;
    localtime_r(&tt, &lt);
    in.epoch = int64_t(tt) + lt.tm_gmtoff;   // Ortszeit, wie sie später der Companion schickt
    {
      std::lock_guard<std::mutex> g(albumMu);
      if (quizNew) {   // neue Rätsel-Fragen vom Portal (Text muss leben, solange das Paket ihn nutzt)
        quizNew = false;
        static std::string live;
        live = quizText;
        kids.pack.load(live.c_str());
        printf("Fragen: %d\n", kids.pack.count());
      }
      if (albumNew) {   // neues Kartenthema vom Portal
        albumNew = false;
        const bool builtin = albumKey.rfind("pferde ", 0) == 0;
        portalImages.reload();
        kids.album.setImages(builtin ? static_cast<ImageStore*>(&kids.images) : &portalImages);
        kids.riddle.setAlbum(&kids.album, builtin ? static_cast<ImageStore*>(&kids.images) : &portalImages);
        kids.album.load(albumText.c_str());
        printf("Album: %s, %d Karten\n", kids.album.title(), kids.album.count());
      }
    }
    app.tick(in);
    sync.frame(app, gRadio, app.currentTitle(), starsGet(store), now, in.epoch, T_WAKE);
    if (gMsgs.recording()) {   // kein Mikrofon im Simulator: Ton erzeugen (M halten = lauter)
      static float ph = 0;
      int16_t b[600];
      const int n = std::min(600, int(Messages::RATE * 0.033f));
      for (int i = 0; i < n; i++) { ph += 0.06f; b[i] = int16_t((in.voice > 0 ? 6000 : 800) * std::sin(ph)); }
      gMsgs.feed(b, n);
    }
    app.draw(scene);
    renderAll();
    if (app.screenOff()) std::memset(fb, 0, sizeof fb);
    SDL_UpdateTexture(tex, nullptr, fb, W * 2);
    SDL_SetTextureColorMod(tex, uint8_t(55 + app.brightness() * 200 / 255), uint8_t(55 + app.brightness() * 200 / 255), uint8_t(55 + app.brightness() * 200 / 255));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    if (now - lastLog > 2000) { lastLog = now; printf("%s\n", app.debug()); }
  }
  SDL_Quit();
  return 0;
}
