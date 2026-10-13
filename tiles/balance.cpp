#include "balance.h"

#include <cmath>
#include <cstdio>

#include "fonts.h"
#include "rewards.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4);
static const Col FIELD = hex(0x111a2e), RIM = hex(0x1f2c4c), PEG_TOP = hex(0xa898f0), PEG_BOT = hex(0x6a58c8);
static const Col BALL_TOP[3] = {hex(0xffdf80), hex(0xffb3d1), hex(0xa4f4d6)};
static const Col BALL_BOT[3] = {hex(0xe8a020), hex(0xe0608e), hex(0x3fae96)};

// Spielfeld: abgerundetes Rechteck innerhalb der runden Displayecken
static constexpr float FL = 14, FT = 14, FR = W - 14, FB = H - 14, FCR = 60;
static constexpr float BR = 14;                  // Kugelradius
static constexpr float G_PX = 1100;              // px/s² pro g Neigung
static constexpr float DEAD = 0.012f;            // g, darunter bleibt die Kugel liegen
static constexpr int SESSION_LEVELS = 4;         // danach Pause

struct LevelDef {
  int nBalls; float sx[3], sy[3];
  int nHoles; float hx[2], hy[2], hr[2];
  int nPegs; float px[6], py[6], pr[6];
};
static const LevelDef LEVELS[] = {
    {1, {184}, {110}, 1, {184}, {330}, {32}, 0, {}, {}, {}},
    {1, {90}, {100}, 1, {280}, {340}, {28}, 1, {184}, {224}, {30}},
    {2, {120, 250}, {100, 100}, 1, {184}, {340}, {30}, 0, {}, {}, {}},
    {1, {80}, {370}, 1, {290}, {100}, {24}, 3, {150, 230, 110}, {250, 180, 150}, {24, 24, 18}},
    {2, {70, 300}, {90, 360}, 1, {184}, {224}, {26}, 4, {184, 184, 100, 268}, {140, 308, 224, 224}, {20, 20, 20, 20}},
    {3, {100, 184, 268}, {90, 90, 90}, 1, {90}, {360}, {28}, 3, {184, 260, 130}, {180, 270, 250}, {26, 24, 20}},
    {2, {184, 184}, {90, 370}, 2, {90, 278}, {224, 224}, {24, 24}, 1, {184}, {224}, {30}},
    {3, {90, 184, 278}, {90, 80, 90}, 1, {184}, {380}, {24}, 5, {120, 248, 184, 80, 288}, {290, 290, 200, 200, 200}, {22, 22, 26, 16, 16}},
};
static constexpr int N_LEVELS = sizeof(LEVELS) / sizeof(LEVELS[0]);

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

Balance::Balance(Synth& synth, Store& store) : synth_(synth), store_(store) {
  level_ = store_.get("bal_lvl", 0) % N_LEVELS;
  dir_ = store_.get("bal_dir", 0) & 3;
  swap_ = store_.get("bal_swap", 0) != 0;
  tilt_ = store_.get("bal_tilt", 0) != 0;
}

void Balance::settingText(int i, char* buf, int n) const {
  static const char* const D[4] = {"normal", "links/rechts gedreht", "oben/unten gedreht", "beides gedreht"};
  if (i == 0) snprintf(buf, n, "Steuerung: %s", tilt_ ? "Neigen" : "Finger");
  else if (i == 1) snprintf(buf, n, "Neigung: %s", D[dir_]);
  else snprintf(buf, n, "Achsen tauschen: %s", swap_ ? "ja" : "nein");
}

bool Balance::settingTap(int i) {
  if (i == 0) { tilt_ = !tilt_; store_.set("bal_tilt", tilt_); }
  else if (i == 1) { dir_ = (dir_ + 1) & 3; store_.set("bal_dir", dir_); }
  else { swap_ = !swap_; store_.set("bal_swap", swap_); }
  return false;
}

void Balance::enter(uint32_t) {
  inSession_ = 0;
  startLevel();
}

void Balance::startLevel() {
  const LevelDef& L = LEVELS[level_];
  nBalls_ = L.nBalls;
  for (int i = 0; i < nBalls_; i++) balls_[i] = {L.sx[i], L.sy[i], 0, 0, false, 0, -1};
  phase_ = START;
  t_ = 0;
}

bool Balance::ballPos(int i, float& x, float& y) const {
  if (i >= nBalls_ || balls_[i].in) return false;
  x = balls_[i].x; y = balls_[i].y;
  return true;
}
void Balance::holePos(int i, float& x, float& y) const {
  const LevelDef& L = LEVELS[level_];
  i = i < L.nHoles ? i : 0;
  x = L.hx[i]; y = L.hy[i];
}

void Balance::bump(float v) {
  if (v < 60 || bumpCd_ > 0) return;
  bumpCd_ = 0.08f;
  Synth::Voice a; a.wave = Synth::NOISE; a.filt = Synth::BP; a.cut0 = a.cut1 = 1600; a.q = 2; a.dur = 0.04f; a.vol = clampf(v / 900, 0.03f, 0.18f);
  synth_.play(a);
}

void Balance::physics(float ax, float ay, float dt) {
  const LevelDef& L = LEVELS[level_];
  const float ix0 = FL + FCR, ix1 = FR - FCR, iy0 = FT + FCR, iy1 = FB - FCR, room = FCR - BR;
  for (int i = 0; i < nBalls_; i++) {
    Ball& b = balls_[i];
    if (b.in) { b.sink = std::fmin(1.f, b.sink + dt / 0.35f); continue; }
    float fx = ax, fy = ay;
    // Trichter: nah am Loch zieht es die Kugel sanft hinein
    for (int h = 0; h < L.nHoles; h++) {
      const float dx = L.hx[h] - b.x, dy = L.hy[h] - b.y, d = std::sqrt(dx * dx + dy * dy) + 1e-3f;
      const float reach = L.hr[h] + BR;
      if (d < reach) {
        const float pull = 700 * (1 - d / reach);
        fx += dx / d * pull; fy += dy / d * pull;
        if (d < L.hr[h] - BR * 0.35f) {   // hineingefallen
          b.in = true; b.hole = h; b.sink = 0; b.x = L.hx[h] + (b.x - L.hx[h]) * 0.5f; b.y = L.hy[h] + (b.y - L.hy[h]) * 0.5f;
          Synth::Voice v; v.wave = Synth::SINE; v.f0 = 640; v.f1 = 220; v.dur = 0.18f; v.vol = 0.2f; synth_.play(v);
          break;
        }
      }
    }
    if (b.in) continue;
    b.vx += fx * dt; b.vy += fy * dt;
    const float damp = std::exp(-1.1f * dt);
    b.vx *= damp; b.vy *= damp;
    // Haftreibung: auf fast ebenem Feld bleibt die Kugel liegen
    if (fx * fx + fy * fy < 30 * 30 && b.vx * b.vx + b.vy * b.vy < 6 * 6) { b.vx = b.vy = 0; }
    b.x += b.vx * dt; b.y += b.vy * dt;
    // Rand (abgerundetes Rechteck)
    const float qx = clampf(b.x, ix0, ix1), qy = clampf(b.y, iy0, iy1);
    float dx = b.x - qx, dy = b.y - qy;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d > room) {
      const float nx = dx / d, ny = dy / d;
      b.x = qx + nx * room; b.y = qy + ny * room;
      const float vn = b.vx * nx + b.vy * ny;
      if (vn > 0) { b.vx -= 1.45f * vn * nx; b.vy -= 1.45f * vn * ny; bump(vn); }
    }
    // Steine
    for (int p = 0; p < L.nPegs; p++) {
      const float ex = b.x - L.px[p], ey = b.y - L.py[p], e = std::sqrt(ex * ex + ey * ey), min = L.pr[p] + BR;
      if (e < min && e > 1e-3f) {
        const float nx = ex / e, ny = ey / e;
        b.x = L.px[p] + nx * min; b.y = L.py[p] + ny * min;
        const float vn = b.vx * nx + b.vy * ny;
        if (vn < 0) { b.vx -= 1.5f * vn * nx; b.vy -= 1.5f * vn * ny; bump(-vn); }
      }
    }
  }
  // Kugeln untereinander
  for (int i = 0; i < nBalls_; i++)
    for (int j = i + 1; j < nBalls_; j++) {
      Ball &a = balls_[i], &c = balls_[j];
      if (a.in || c.in) continue;
      const float dx = c.x - a.x, dy = c.y - a.y, d = std::sqrt(dx * dx + dy * dy);
      if (d < 2 * BR && d > 1e-3f) {
        const float nx = dx / d, ny = dy / d, push = (2 * BR - d) * 0.5f;
        a.x -= nx * push; a.y -= ny * push; c.x += nx * push; c.y += ny * push;
        const float rel = (c.vx - a.vx) * nx + (c.vy - a.vy) * ny;
        if (rel < 0) {
          const float k = -0.9f * rel;
          a.vx -= nx * k; a.vy -= ny * k; c.vx += nx * k; c.vy += ny * k;
          bump(-rel);
        }
      }
    }
}

void Balance::tick(const TileInput& in) {
  const float dt = in.dt;
  t_ += dt;
  clock_ += dt;
  bumpCd_ = std::fmax(0, bumpCd_ - dt);
  // Neigung in Bildschirmrichtung: rechte Kante runter → ax negativ → Kugel nach rechts
  float tx = in.tiltX, ty = in.tiltY;
  if (swap_) { const float t = tx; tx = ty; ty = t; }
  if (dir_ & 1) tx = -tx;
  if (dir_ & 2) ty = -ty;
  switch (phase_) {
    case START:
      // Haltung der ersten Sekunde als „gerade“ mitteln
      baseX_ = t_ < 0.1f ? tx : baseX_ + (tx - baseX_) * 0.2f;
      baseY_ = t_ < 0.1f ? ty : baseY_ + (ty - baseY_) * 0.2f;
      gx_ = gy_ = 0;
      if (t_ > 1.2f || (in.tap && t_ > 0.3f)) { phase_ = PLAY; t_ = 0; }
      break;
    case PLAY: {
      float gx = -(tx - baseX_), gy = ty - baseY_;
      finger_ = in.finger;
      fx_ = in.fingerX; fy_ = in.fingerY;
      if (!tilt_) {   // Finger als Joystick: Abstand zur Mitte = Neigung, losgelassen = eben
        gx = finger_ ? clampf((fx_ - W / 2) / 170.f, -1, 1) * 0.3f : 0;
        gy = finger_ ? clampf((fy_ - H / 2) / 190.f, -1, 1) * 0.3f : 0;
      }
      const float m = std::sqrt(gx * gx + gy * gy);
      if (m < DEAD) gx = gy = 0;
      else { const float k = (m - DEAD) / m; gx *= k; gy *= k; }
      gx_ = gx; gy_ = gy;
      for (int s = 0; s < 3; s++) physics(gx * G_PX, gy * G_PX, dt / 3);
      bool all = true;
      for (int i = 0; i < nBalls_; i++) all = all && balls_[i].in && balls_[i].sink >= 1;
      if (all) {
        phase_ = DONE; t_ = 0;
        stars_ = starsAdd(store_);
        inSession_++;
        level_ = (level_ + 1) % N_LEVELS;
        store_.set("bal_lvl", level_);
        for (int k = 0; k < 3; k++) { Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = 660.f * (1 + 0.25f * k); v.delay = 0.1f * k; v.dur = 0.3f; v.vol = 0.12f; synth_.play(v); }
      }
      break;
    }
    case DONE:
      if (t_ > 2.6f || (in.tap && t_ > 0.8f)) {
        if (inSession_ >= SESSION_LEVELS) { phase_ = PAUSE; t_ = 0; }
        else startLevel();
      }
      break;
    case PAUSE:
      if (t_ > 4 || (in.tap && t_ > 1)) {
        requestGoto(GOTO_BACK);
        inSession_ = 0;
        startLevel();
      }
      break;
  }
}

void Balance::draw(Scene& s) {
  const LevelDef& L = (phase_ == DONE || phase_ == PAUSE) ? LEVELS[(level_ + N_LEVELS - 1) % N_LEVELS] : LEVELS[level_];
  // Spielfeld
  s.rrect(FL - 2, FT - 2, FR - FL + 4, FB - FT + 4, FCR + 2, RIM);
  s.rrect(FL, FT, FR - FL, FB - FT, FCR, FIELD);
  // Löcher
  for (int h = 0; h < L.nHoles; h++) {
    const float pulse = 0.5f + 0.5f * std::sin(clock_ * 2 + h);
    s.glow(L.hx[h], L.hy[h], L.hr[h] * 1.9f, hex(0x4fd1c5), 0.18f + 0.08f * pulse, L.hr[h]);
    s.circle(L.hx[h], L.hy[h], L.hr[h] + 2, hex(0x3a4a70));
    s.ellipse(L.hx[h], L.hy[h], L.hr[h], L.hr[h], hex(0x02040a), hex(0x0c1424));
  }
  // Steine
  for (int p = 0; p < L.nPegs; p++) {
    s.ellipse(L.px[p] + 3, L.py[p] + 4, L.pr[p], L.pr[p] * 0.9f, hex(0x070b14), 0.6f);
    s.ellipse(L.px[p], L.py[p], L.pr[p], L.pr[p], PEG_TOP, PEG_BOT);
    s.ellipse(L.px[p] - L.pr[p] * 0.35f, L.py[p] - L.pr[p] * 0.4f, L.pr[p] * 0.3f, L.pr[p] * 0.16f, hex(0xffffff), 0.25f);
  }
  // Kugeln (bei DONE/PAUSE sind alle drin)
  if (phase_ == START || phase_ == PLAY)
    for (int i = 0; i < nBalls_; i++) {
      const Ball& b = balls_[i];
      const float r = BR * (b.in ? 1 - 0.55f * b.sink : 1), a = b.in ? 1 - b.sink : 1;
      if (a <= 0.01f) continue;
      if (!b.in) s.ellipse(b.x + 3 - gx_ * 6, b.y + 5 + gy_ * 6, r, r * 0.85f, hex(0x05080f), 0.55f);
      s.ellipse(b.x, b.y, r, r, BALL_TOP[i], BALL_BOT[i], a);
      s.ellipse(b.x - r * 0.35f, b.y - r * 0.4f, r * 0.32f, r * 0.2f, hex(0xffffff), 0.7f * a);
      if (b.in)   // Funkeln am Loch
        for (int k = 0; k < 4; k++) {
          const float an = k * TAU / 4 + b.sink * 2, rr = L.hr[b.hole] + 6 + 14 * b.sink;
          s.star(L.hx[b.hole] + rr * std::sin(an), L.hy[b.hole] - rr * std::cos(an), 6, hex(0xffe08a), (1 - b.sink) * 0.9f, an);
        }
    }

  char buf[48];
  switch (phase_) {
    case START: {
      const float a = smooth(t_ / 0.3f) * (1 - smooth((t_ - 0.9f) / 0.3f));
      snprintf(buf, sizeof buf, "Runde %d", level_ + 1);
      s.text(fonts::title, buf, W / 2, 196, TXT, a, Align::Center);
      if (level_ == 0) {
        s.text(fonts::body, tilt_ ? "Kipp mich, dann" : "Die Kugel rollt", W / 2, 240, TXT_DIM, a, Align::Center);
        s.text(fonts::body, tilt_ ? "rollt die Kugel" : "zu deinem Finger", W / 2, 270, TXT_DIM, a, Align::Center);
      }
      break;
    }
    case DONE: {
      const float a = smooth(t_ / 0.4f);
      s.text(fonts::title, "Geschafft!", W / 2, 186, TXT, a, Align::Center);
      drawStarCount(s, W / 2, 236, stars_, smooth((t_ - 0.4f) / 0.4f));
      break;
    }
    case PAUSE: {
      const float a = smooth(t_ / 0.5f);
      s.text(fonts::title, "Super gerollt!", W / 2, 170, TXT, a, Align::Center);
      s.text(fonts::body, "Jetzt eine kleine Pause.", W / 2, 218, TXT_DIM, a, Align::Center);
      drawStarCount(s, W / 2, 262, stars_, a);
      break;
    }
    case PLAY:
      if (!tilt_ && finger_) {   // Joystick-Anzeige: Ring am Finger, Linie von der Mitte
        s.capsule(W / 2, H / 2, fx_, fy_, 2, hex(0x4fd1c5), 0.25f);
        s.ring(fx_, fy_, 26, 3, hex(0x4fd1c5), 0.5f);
        s.circle(W / 2, H / 2, 4, hex(0x4fd1c5), 0.4f);
      }
      break;
  }
}

}  // namespace dk
