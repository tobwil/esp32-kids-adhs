// Simulator für den Mac (SDL2): dieselbe App wie auf dem Gerät, Beschleunigung wird nachgebildet.
// Maus = Finger (ziehen = wischen, lange drücken = Einstellungen), B = BOOT-Taste, Pfeiltasten = neigen, C = gerade,
// Leertaste = auf den Tisch klopfen, 1/2/3 = leicht/mittel/fest klopfen, T (halten) = Tastatur tippen,
// X (halten) = schütteln, P = Bildschirmfoto, Esc = Ende.
//
// Ohne Fenster:
//   dksim --screens <ordner>   Ablauf durchspielen und Bilder als BMP speichern
//   dksim --bench              Rasterzeit pro Frame messen
#include <SDL.h>

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

// Kinder-Aufbau: 0 Check-in (Start), 1 Grummel, 2 Atmen, 3 Kugel ins Loch, 4 Pferde-Album
struct Kids {
  CheckIn checkin;
  Grummel grummel;
  Breathe breathe;
  Balance balance;
  Album album;
  FileImages images;
  Kids(Synth& synth, Store& store, uint32_t seed)
      : checkin(synth, store), grummel(synth, store, seed, KID_SKIN), breathe(synth, store), balance(synth, store), album(synth, store, seed * 7 + 3) {
    album.setImages(&images);
    album.load(ALBUM_BUILTIN);
    checkin.setAlbum(4);
    checkin.setTargets(1, 2);
    grummel.setHome(0);
    breathe.setHome(0);
    balance.setHome(0);
  }
  bool startOff = true;
  void addTo(App& app) {
    app.add(&checkin); app.add(&grummel); app.add(&breathe); app.add(&balance); app.add(&album);
    app.setHome(0);
    app.setSettingsHold(3000);   // Einstellungen nur für Eltern: 3 s halten oder BOOT
    app.setShowTitles(false);
    app.setPower(60000, 180000, startOff, false);   // Kinder: aus bis zum Tippen, nach 3 min wieder schwarz
  }
};

static int screens(const char* dir) {
  Synth synth;
  FileStore store("");
  App app(synth, store);
  Kids kids(synth, store, 7);
  kids.startOff = false;
  kids.addTo(app);
  Grummel& grummel = kids.grummel;
  Script sc{app, {}, {}, 1000, {}, dir};
  app.begin(sc.ms);
  auto tapFace = [&](int i) { float x, y; CheckIn::faceCenter(i, x, y); sc.tap(int(x), int(y)); };
  int guard = 0;

  sc.run(2.0f); sc.shot("01_checkin");
  tapFace(CheckIn::WUT); sc.run(0.9f); sc.shot("02_wut");
  sc.run(3.5f); sc.shot("03_grummel");
  sc.run(1.5f); sc.shot("03b_hinweis");
  // schnelles Tippen an wechselnden Stellen
  static const int TX[6] = {90, 280, 120, 250, 184, 70}, TY[6] = {120, 140, 340, 330, 90, 240};
  for (int i = 0; i < 6; i++) { sc.tap(TX[i], TY[i]); sc.run(0.12f); }
  sc.shot("04_laden");
  int taps = 6;
  while (grummel.phase() == Grummel::CHARGE && guard++ < 80) { sc.tap(TX[taps % 6], TY[taps % 6]); taps++; sc.run(0.15f); }
  printf("Tipps bis satt: %d\n", taps);
  sc.run(1.1f); sc.shot("05_boerps");
  while (grummel.phase() != Grummel::BREATHE && guard++ < 900) sc.run(0.1f);
  sc.run(2.8f); sc.shot("06_atmen_ein");
  sc.run(4.5f); sc.shot("07_atmen_aus");
  while (grummel.phase() != Grummel::THANKS && guard++ < 2000) sc.run(0.1f);
  sc.run(1.2f); sc.shot("08_lecker");
  while (!std::strstr(app.debug(), "Wie geht") && guard++ < 2200) sc.run(0.1f);
  sc.run(1.0f); sc.shot("09_checkin_jetzt");
  tapFace(CheckIn::KRIBBELIG); sc.run(0.9f); sc.shot("10_kribbelig");
  sc.run(4.0f); sc.shot("11_ballon_start");
  sc.run(3.2f); sc.shot("12_ballon_ein");
  sc.run(4.0f); sc.shot("13_ballon_aus");
  while (kids.breathe.phase() != Breathe::DONE && guard++ < 3000) sc.run(0.1f);
  sc.run(1.8f); sc.shot("14_super");
  sc.run(3.0f);
  tapFace(CheckIn::TRAURIG); sc.run(1.2f); sc.shot("15_traurig");
  sc.run(5.0f);
  sc.tap(184, 200); sc.run(1.0f); sc.shot("16a_abgebrochen");   // Tipp bricht ab, zurück zum Check-in
  sc.run(3.0f);
  tapFace(CheckIn::FROH); sc.run(1.5f); sc.shot("16_froh");
  sc.run(4.0f);
  for (int i = 0; i < 5; i++) { sc.imu.knock(sc.ms, 0.3f); sc.run(0.1f); }   // Klopfen im Check-in bewirkt nichts
  sc.touch(true, 184, 420); sc.run(1.2f); sc.shot("17_halten_1s");
  sc.run(2.2f); sc.touch(false, 184, 420); sc.frame(); sc.run(0.5f); sc.shot("18_einstellungen");
  sc.touch(false, 184, 420); sc.frame();
  // Kugel ins Loch: Autopilot neigt das Gerät zur nächsten Kugel → Loch (PD-Regler)
  sc.tap(184, 600); sc.run(0.3f);   // Einstellungen zu (Tipp daneben)
  app.show(3);
  Balance& bal = kids.balance;
  float px = 0, py = 0;
  int shots = 0;
  bool fingerDown = false;
  const int lvl0 = bal.level();
  for (int f = 0; f < 30 * 120 && bal.level() < lvl0 + 4 && bal.phase() != Balance::PAUSE; f++) {
    float bx, by, hx, hy;
    int target = -1;
    for (int i = 0; i < 3 && target < 0; i++) if (bal.ballPos(i, bx, by)) target = i;
    if (bal.phase() == Balance::PLAY && target >= 0) {
      bal.holePos(0, hx, hy);
      if (bal.ballPos(1, px, py) && bal.level() == 6 && target == 1) bal.holePos(1, hx, hy);
      static float lx = bx, ly = by;
      const float vx = (bx - lx) * 30, vy = (by - ly) * 30;
      lx = bx; ly = by;
      // Finger als Joystick: Versatz von der Mitte gibt die Richtung vor (PD-Regler)
      const float fx = 184 + std::fmax(-170.f, std::fmin(170.f, 1.6f * (hx - bx) - 0.25f * vx));
      const float fy = 224 + std::fmax(-190.f, std::fmin(190.f, 1.8f * (hy - by) - 0.25f * vy));
      sc.touch(true, int(fx), int(fy));
      fingerDown = true;
    } else if (fingerDown) {
      sc.touch(false, 184, 224);
      fingerDown = false;
    }
    sc.frame();
    if (getenv("DKDEBUG") && f % 30 == 0) { float bx2 = 0, by2 = 0; bal.ballPos(0, bx2, by2); printf("f %d phase %d ball %.0f %.0f  %s\n", f, int(bal.phase()), bx2, by2, app.debug()); }
    if (f == 40) sc.shot("19_runde");
    static int doneShot = 0, lastLvl = -1;
    if (bal.phase() == Balance::PLAY && bal.level() != lastLvl && bal.level() >= 1 && shots < 3) { lastLvl = bal.level(); sc.run(0.8f); char n[32]; snprintf(n, sizeof n, "2%d_rollen", shots++); sc.shot(n); }
    if (bal.phase() == Balance::DONE && !doneShot) { sc.run(0.6f); sc.shot("23_geschafft"); doneShot = 1; }
  }
  printf("Kugel ins Loch: von Runde %d bis Runde %d gelöst, Phase %d\n", lvl0 + 1, bal.level(), int(bal.phase()));
  sc.imu.tiltX = sc.imu.tiltY = 0;
  while (bal.phase() != Balance::PAUSE && bal.phase() != Balance::START && guard++ < 6000) sc.run(0.1f);
  // Pferde-Album: Sterne sammeln, Karte aufdecken, blättern, Karte ansehen und umdrehen
  while (starsGet(store) < 9) starsAdd(store);
  app.show(0); sc.run(1.0f); sc.shot("40_checkin_neue_karte");
  sc.tap(184, 418); sc.run(1.2f); sc.shot("41_verdeckt");
  sc.tap(184, 214); sc.run(0.6f); sc.shot("42_aufgedeckt");
  sc.run(1.5f); sc.tap(184, 214); sc.run(0.6f); sc.shot("43_rueckseite");
  sc.tap(184, 214); sc.run(0.6f);
  for (int k = 0; k < 12 && kids.album.phase() != Album::GRID; k++) { sc.tap(184, 214); sc.run(1.2f); }   // weitere Karten aufdecken
  sc.run(1.0f); sc.shot("44_album");
  sc.tap(W - 70, 404); sc.run(0.6f); sc.shot("45_album_seite2");
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
  app.show(1);   // Grummel
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
