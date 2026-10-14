#include "post.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4), ROW = hex(0x1e2442), ROW_NEW = hex(0x2a2f5c), INK = hex(0x1a1238);
static const Col REC = hex(0xff7a9a), REC_ON = hex(0xff4f6e), GOOD = hex(0x4cc47a), GOLD = hex(0xffd166);
static const uint32_t AVATAR[5] = {0xff9ec4, 0x9cc4ff, 0xffd166, 0x9ff0b8, 0xc9bcff};
static constexpr int ROWS = 3;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static inline bool hit(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r; }

static Col avatarCol(const char* name) {
  uint32_t h = 7;
  for (const char* c = name; *c; c++) h = h * 31 + uint8_t(*c);
  return hex(AVATAR[h % 5]);
}

// erster Buchstabe (UTF-8-sicher: ganzes Zeichen)
static void initial(const char* name, char* out) {
  int n = 1;
  const uint8_t c = uint8_t(name[0]);
  if (c >= 0xC0) n = c >= 0xE0 ? 3 : 2;
  std::memcpy(out, name, n);
  out[n] = 0;
}

void Post::rowRect(int i, float& x, float& y, float& w, float& h) { x = 24; w = W - 48; h = 62; y = 78 + i * 72; }

void Post::contactPos(int i, int n, float& x, float& y) {
  x = W / 2 + (i - (n - 1) * 0.5f) * 84;
  y = 352;
}

void Post::chime(bool up) {
  for (int k = 0; k < 2; k++) {
    Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = up ? 660.f + 220 * k : 880.f - 220 * k; v.delay = 0.1f * k; v.dur = 0.2f; v.vol = 0.12f;
    synth_.play(v);
  }
}

void Post::tick(const TileInput& in) {
  t_ += in.dt;
  clock_ += in.dt;
  voice_ += (in.voice - voice_) * clampf(in.dt * 10, 0, 1);
  if (!r_.clipPlaying()) playing_[0] = 0;
  switch (phase_) {
    case LIST: {
      if (!in.tap) break;
      const int n = m_.count() < ROWS ? m_.count() : ROWS;
      for (int i = 0; i < n; i++) {   // Nachricht abspielen oder anhalten
        float x, y, w, h;
        rowRect(i, x, y, w, h);
        if (in.tapX < x || in.tapX > x + w || in.tapY < y || in.tapY > y + h) continue;
        Message msg;
        if (!m_.get(i, msg)) return;
        if (r_.clipPlaying() && !std::strcmp(playing_, msg.id)) { r_.stopClip(); playing_[0] = 0; return; }
        char path[48];
        snprintf(path, sizeof path, "/nachricht/%s.pcm", msg.id);
        r_.playClip(path);
        snprintf(playing_, sizeof playing_, "%s", msg.id);
        m_.markHeard(msg.id);
        return;
      }
      const int nc = m_.contacts();
      for (int i = 0; i < nc; i++) {
        float x, y;
        contactPos(i, nc, x, y);
        if (hit(in.tapX, in.tapY, x, y, 40) && m_.contact(i, to_)) { phase_ = RECORD; t_ = 0; m_.discard(); r_.stopClip(); return; }
      }
      break;
    }
    case RECORD: {
      if (!in.tap || t_ < 0.3f) break;
      const bool have = !m_.recording() && m_.recordedSecs() >= 0.5f;
      if (hit(in.tapX, in.tapY, REC_X, REC_Y, REC_R + 12) && !have) {
        if (m_.recording()) { m_.stopRecording(); chime(false); }
        else if (m_.startRecording()) chime(true);
      } else if (have && hit(in.tapX, in.tapY, REC_X - ACT_DX, ACT_Y, ACT_R + 10)) {
        m_.discard();   // nochmal
      } else if (have && hit(in.tapX, in.tapY, REC_X + ACT_DX, ACT_Y, ACT_R + 10)) {
        if (m_.send(to_.id)) { phase_ = SENT; t_ = 0; offline_ = !r_.online(); chime(true); }
      }
      break;
    }
    case SENT:
      if (t_ > 3.2f || (in.tap && t_ > 1)) { phase_ = LIST; t_ = 0; }
      break;
  }
}

// ---------------- Zeichnen ----------------

static void envelope(Scene& s, float x, float y, float w, float a) {
  const float h = w * 0.66f;
  s.rrect(x - w / 2, y - h / 2, w, h, w * 0.08f, hex(0xfff3d6), a);
  s.capsule(x - w / 2 + 3, y - h / 2 + 3, x, y + h * 0.08f, 1.6f, hex(0xc8a070), a);
  s.capsule(x + w / 2 - 3, y - h / 2 + 3, x, y + h * 0.08f, 1.6f, hex(0xc8a070), a);
}

static void mic(Scene& s, float x, float y, float k, Col c) {
  s.capsule(x, y - 16 * k, x, y + 2 * k, 9 * k, c);
  s.arc(x, y - 2 * k, 15 * k, 3 * k, PIF / 2 + 0.2f, PIF * 1.5f - 0.2f, c);
  s.capsule(x, y + 14 * k, x, y + 22 * k, 2.5f * k, c);
  s.capsule(x - 8 * k, y + 22 * k, x + 8 * k, y + 22 * k, 2.5f * k, c);
}

static void avatar(Scene& s, const char* name, float x, float y, float r) {
  char b[5];
  initial(name, b);
  s.circle(x, y, r, avatarCol(name));
  const fonts::Font& f = r > 26 ? fonts::title : fonts::body;
  s.text(f, b, x, y - f.lineH / 2.f + 1, INK, 1, Align::Center);
}

void Post::draw(Scene& s) {
  if (phase_ == SENT) {
    const float u = smooth(t_ / 1.2f);
    envelope(s, W / 2 + 120 * u, 190 - 90 * u, 110 * (1 - 0.5f * u), 1 - smooth((t_ - 0.9f) / 0.4f));
    for (int k = 0; k < 6; k++) {
      const float an = k * TAU / 6 + t_, rr = 60 + 50 * u;
      s.star(W / 2 + rr * std::sin(an), 190 - rr * std::cos(an), 6, GOLD, 0.8f * (1 - u * 0.6f), an);
    }
    s.text(fonts::title, "Gesendet!", W / 2, 286, TXT, smooth((t_ - 0.4f) / 0.4f), Align::Center);
    char b[48];
    snprintf(b, sizeof b, "%s hört sie bald.", to_.name);
    if (offline_) {
      s.text(fonts::small, "Sobald das WLAN da ist,", W / 2, 336, TXT_DIM, 1, Align::Center);
      s.text(fonts::small, "schickt Grummel sie los.", W / 2, 362, TXT_DIM, 1, Align::Center);
    } else {
      s.text(fonts::body, b, W / 2, 336, TXT_DIM, smooth((t_ - 0.6f) / 0.4f), Align::Center);
    }
    return;
  }

  if (phase_ == RECORD) {
    char b[48];
    snprintf(b, sizeof b, "An %s", to_.name);
    s.text(Scene::textW(fonts::title, b) < 290 ? fonts::title : fonts::body, b, W / 2, 34, TXT, 1, Align::Center);
    const bool rec = m_.recording(), have = !rec && m_.recordedSecs() >= 0.5f;
    const float secs = m_.recordedSecs(), frac = secs / Messages::MAX_SECS;
    if (rec)   // Schallwellen vom Mikrofonpegel
      for (int k = 0; k < 3; k++) {
        const float u = std::fmod(clock_ * 0.8f + k / 3.f, 1.f);
        s.ring(REC_X, REC_Y, REC_R + 10 + u * 40, 3, REC, (1 - u) * (0.2f + 0.6f * voice_));
      }
    s.ring(REC_X, REC_Y, REC_R + 8, 5, hex(0x3a2440));
    if (secs > 0) s.arc(REC_X, REC_Y, REC_R + 8, 5, 0, std::fmax(0.05f, frac * TAU), have ? GOOD : REC_ON);
    s.circle(REC_X, REC_Y, REC_R, rec ? REC_ON : have ? hex(0x3a3460) : REC);
    if (rec) s.rrect(REC_X - 18, REC_Y - 18, 36, 36, 7, TXT);
    else if (have) envelope(s, REC_X, REC_Y, 64, 1);
    else mic(s, REC_X, REC_Y, 1.6f, INK);
    snprintf(b, sizeof b, "%d s", int(secs + 0.5f));
    if (rec || have) s.text(fonts::body, b, W / 2, REC_Y + REC_R + 18, TXT_DIM, 1, Align::Center);
    if (!rec && !have) s.text(fonts::body, "Tippen und sprechen", W / 2, REC_Y + REC_R + 18, TXT_DIM, 0.6f + 0.3f * std::sin(clock_ * 2.5f), Align::Center);
    if (rec) s.text(fonts::small, "Nochmal tippen, wenn du fertig bist", W / 2, REC_Y + REC_R + 52, TXT_DIM, 1, Align::Center);
    if (have) {
      // Nochmal (links) und Senden (rechts)
      s.circle(REC_X - ACT_DX, ACT_Y, ACT_R, hex(0x2a2a52));
      s.arc(REC_X - ACT_DX, ACT_Y, 13, 3.5f, 0.6f, TAU - 0.4f, TXT);
      s.capsule(REC_X - ACT_DX + 7, ACT_Y - 12, REC_X - ACT_DX + 13, ACT_Y - 5, 3, TXT);
      s.text(fonts::small, "Nochmal", REC_X - ACT_DX, ACT_Y + ACT_R + 4, TXT_DIM, 1, Align::Center);
      s.circle(REC_X + ACT_DX, ACT_Y, ACT_R, GOOD);
      const float px = REC_X + ACT_DX, py = ACT_Y;   // Papierflieger
      s.capsule(px - 14, py + 2, px + 14, py - 10, 3, INK);
      s.capsule(px + 14, py - 10, px + 2, py + 12, 3, INK);
      s.capsule(px - 14, py + 2, px + 2, py + 12, 3, INK);
      s.text(fonts::small, "Senden", REC_X + ACT_DX, ACT_Y + ACT_R + 4, TXT_DIM, 1, Align::Center);
    }
    return;
  }

  // Liste
  s.text(fonts::title, "Post", W / 2, 22, TXT, 1, Align::Center);
  const int n = m_.count() < ROWS ? m_.count() : ROWS;
  for (int i = 0; i < n; i++) {
    Message msg;
    if (!m_.get(i, msg)) break;
    float x, y, w, h;
    rowRect(i, x, y, w, h);
    s.rrect(x, y, w, h, 20, msg.heard ? ROW : ROW_NEW);
    avatar(s, msg.from, x + 36, y + h / 2, 22);
    char b[48];
    snprintf(b, sizeof b, "von %s", msg.from);
    s.text(fonts::body, b, x + 70, y + 8, TXT);
    s.text(fonts::small, msg.heard ? "angehört" : "neu", x + 70, y + 34, msg.heard ? TXT_DIM : GOLD);
    const bool on = r_.clipPlaying() && !std::strcmp(playing_, msg.id);
    const float px = x + w - 34, py = y + h / 2;
    s.circle(px, py, 20, on ? GOOD : hex(0x3e3e78));
    if (on) {
      s.capsule(px - 5, py - 7, px - 5, py + 7, 3, INK);
      s.capsule(px + 5, py - 7, px + 5, py + 7, 3, INK);
    } else {
      s.capsule(px - 5, py - 9, px + 8, py, 3, TXT);
      s.capsule(px - 5, py + 9, px + 8, py, 3, TXT);
      s.capsule(px - 5, py - 9, px - 5, py + 9, 3, TXT);
    }
  }
  if (!n) {
    envelope(s, W / 2, 150, 80, 0.5f);
    s.text(fonts::body, "Noch keine Post.", W / 2, 196, TXT_DIM, 1, Align::Center);
  }
  const int nc = m_.contacts();
  if (nc) {
    s.text(fonts::small, "Schick eine Nachricht an:", W / 2, 296, TXT_DIM, 1, Align::Center);
    for (int i = 0; i < nc; i++) {
      Contact c;
      if (!m_.contact(i, c)) break;
      float x, y;
      contactPos(i, nc, x, y);
      avatar(s, c.name, x, y, 30);
      char nm[16];
      snprintf(nm, sizeof nm, "%.10s", c.name);
      s.text(fonts::small, nm, x, y + 34, TXT, 1, Align::Center);
    }
  } else {
    s.text(fonts::small, "Kontakte legen die Eltern", W / 2, 320, TXT_DIM, 1, Align::Center);
    s.text(fonts::small, "im Portal an.", W / 2, 346, TXT_DIM, 1, Align::Center);
  }
}

}  // namespace dk
