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
#include "album_builtin.h"
#include "rewards.h"

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

// Kinder-Aufbau: 0 Dashboard, 1 Check-in, 2 Grummel, 3 Atmen, 4 Kugel ins Loch, 5 Pferde-Album
enum { T_HOME, T_CHECKIN, T_GRUMMEL, T_BREATHE, T_BALANCE, T_ALBUM };
static const Home::AppIcon APPS[] = {
    {"Gefühle", T_CHECKIN, 0}, {"Grummel", T_GRUMMEL, 1}, {"Atmen", T_BREATHE, 2}, {"Kugel", T_BALANCE, 3}, {"Pferde", T_ALBUM, 4}};

struct Kids {
  Home home;
  CheckIn checkin;
  Grummel grummel;
  Breathe breathe;
  Balance balance;
  Album album;
  FileImages images;
  Kids(Synth& synth, Store& store, uint32_t seed)
      : home(synth, store), checkin(synth, store), grummel(synth, store, seed, KID_SKIN), breathe(synth, store), balance(synth, store), album(synth, store, seed * 7 + 3) {
    album.setImages(&images);
    album.load(ALBUM_BUILTIN);
    home.setApps(APPS, 5);
    home.setCheckIn(T_CHECKIN);
    checkin.setTargets(T_GRUMMEL, T_BREATHE);
  }
  bool startOff = true;
  void addTo(App& app) {
    app.add(&home); app.add(&checkin); app.add(&grummel); app.add(&breathe); app.add(&balance); app.add(&album);
    app.setHome(T_HOME);
    app.setSettingsHold(3000);   // Einstellungen nur für Eltern: 3 s halten oder BOOT
    app.setPower(60000, 180000, startOff, false);   // aus bis zum Tippen, nach 3 min wieder schwarz
  }
};

// ---- Textprüfung: Rand, runde Ecken, ✕-Knopf ----
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
  if (!why[0] && gApp && gApp->closeShown()) {
    const float qx = std::fmax(x0, std::fmin(App::CLOSE_X, x1)), qy = std::fmax(y0 + 4, std::fmin(App::CLOSE_Y, y1 - 4));
    if ((qx - App::CLOSE_X) * (qx - App::CLOSE_X) + (qy - App::CLOSE_Y) * (qy - App::CLOSE_Y) < (App::CLOSE_R + 3) * (App::CLOSE_R + 3)) snprintf(why, sizeof why, "unter dem ✕");
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
  auto tapIcon = [&](int i) { float x, y; Home::iconPos(i, 5, x, y); sc.tap(int(x), int(y)); };
  auto close = [&]() { sc.tap(int(App::CLOSE_X), int(App::CLOSE_Y)); sc.run(0.5f); };
  int guard = 0;

  sc.run(1.0f); shot("00_dashboard_hallo");
  sc.run(3.0f); shot("01_dashboard_frage");
  // streicheln: Finger hin und her über Grummel
  sc.touch(true, 140, 150); sc.frame();
  for (int k = 0; k < 40; k++) { sc.touch(true, 140 + int(80 * std::fabs(std::sin(k * 0.25f))), 150 + (k % 7)); sc.frame(); }
  shot("02_streicheln");
  sc.touch(false, 180, 150); sc.frame(); sc.run(1.5f);
  sc.tap(int(Home::BUDDY_X), int(Home::BUDDY_Y)); sc.run(0.4f); shot("03_kichern");
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
  // Atmen öffnen und mit ✕ schließen
  tapIcon(2); sc.run(4.0f); shot("15_atmen");
  close(); shot("16_dashboard_nach_x");
  // Kugel ins Loch
  tapIcon(3); sc.run(0.6f); shot("17_kugel_start");
  sc.run(1.0f); sc.touch(true, 184, 400); sc.run(0.8f); shot("18_kugel_finger"); sc.touch(false, 184, 400); sc.frame();
  close();
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
  for (int k = 0; k < 60 && kids.album.phase() != Album::GRID; k++) {
    sc.tap(184, 214); sc.run(0.7f);
    char n[32]; snprintf(n, sizeof n, "z%02d_karte", k); if (k % 2 == 0) shot(n);
  }
  close();
  // Einstellungen: 3 s auf eine freie Stelle drücken
  sc.touch(true, 30, 230); sc.run(3.4f); sc.touch(false, 30, 230); sc.frame(); sc.run(0.5f); shot("25_einstellungen");
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

int main(int argc, char** argv) {
  if (argc > 2 && !strcmp(argv[1], "--screens")) return screens(argv[2]);
  if (argc > 1 && !strcmp(argv[1], "--bench")) return bench();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_Window* win = SDL_CreateWindow("Desk-Kacheln", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren, W, H);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);

  Synth synth;
  SDL_AudioSpec want{}, have{};
  want.freq = Synth::RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
  want.callback = [](void* u, Uint8* stream, int len) { static_cast<Synth*>(u)->render((int16_t*)stream, len / 2); };
  want.userdata = &synth;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);

  std::string home = getenv("HOME") ? getenv("HOME") : ".";
  FileStore store(home + "/.desk-kacheln.txt");
  App app(synth, store);
  Kids kids(synth, store, SDL_GetTicks() * 2654435761u + 1);
  kids.addTo(app);
  app.begin(SDL_GetTicks());

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
    app.tick(in);
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
