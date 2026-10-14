#include "radio.h"

#include <cmath>
#include <cstdio>

#include "fonts.h"
#include "quiz.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4), BTN = hex(0x2a2a52), BTN_RIM = hex(0x3e3e78);
static const Col PLAY_TOP = hex(0xb7a6ff), PLAY_BOT = hex(0x7a62e0), INK = hex(0x1a1238), MOON = hex(0xfff0c8);
enum { B_PLAY, B_PREV, B_NEXT, B_MINUS, B_PLUS, B_SKIP };

static inline bool hit(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r; }

void Radio::tick(const TileInput& in) {
  clock_ += in.dt;
  pressT_ = std::fmax(0, pressT_ - in.dt);
  locked_ = in.locked;
  p_.tick(in.dt);
  if (!in.tap) return;
  const float x = in.tapX, y = in.tapY;
  const int n = p_.count();
  int b = -1;
  if (hit(x, y, PLAY_X, PLAY_Y, PLAY_R + 10)) b = B_PLAY;
  else if (n > 1 && hit(x, y, PREV_X, PLAY_Y, ARROW_R + 14)) b = B_PREV;
  else if (n > 1 && hit(x, y, NEXT_X, PLAY_Y, ARROW_R + 14)) b = B_NEXT;
  else if (hit(x, y, MINUS_X, VOL_Y, VOL_R + 14)) b = B_MINUS;
  else if (hit(x, y, PLUS_X, VOL_Y, VOL_R + 14)) b = B_PLUS;
  else if (std::fabs(y - (SKIP_Y + SKIP_H / 2)) < SKIP_H / 2 + 6 && std::fabs(x - W / 2) < 90) {
    RadioSource s;
    if (p_.playing() && p_.source(p_.current(), s) && s.spotify) b = B_SKIP;
  }
  if (b < 0) return;
  pressed_ = b;
  pressT_ = 0.25f;
  switch (b) {
    case B_PLAY: if (p_.playing()) p_.stop(); else p_.play(); break;
    case B_PREV: p_.select((p_.current() + n - 1) % n); break;
    case B_NEXT: p_.select((p_.current() + 1) % n); break;
    case B_MINUS: p_.setVolume(p_.volume() - 10); break;
    case B_PLUS: p_.setVolume(p_.volume() + 10); break;
    case B_SKIP: p_.skip(); break;
  }
}

// kleine Formen
static void tri(Scene& s, float x, float y, float r, float dir, Col c) {   // Dreieck aus drei Strichen, Spitze in dir (+1 rechts)
  const float ax = x + dir * r, bx = x - dir * r * 0.6f;
  s.capsule(bx, y - r * 0.85f, ax, y, r * 0.22f, c);
  s.capsule(bx, y + r * 0.85f, ax, y, r * 0.22f, c);
  s.capsule(bx, y - r * 0.85f, bx, y + r * 0.85f, r * 0.22f, c);
  s.ellipse(x - dir * r * 0.1f, y, r * 0.42f, r * 0.5f, c);
}

static void moon(Scene& s, float x, float y, float r, Col bg) {
  s.circle(x, y, r, MOON);
  s.circle(x + r * 0.45f, y - r * 0.3f, r * 0.85f, bg);
}

void Radio::draw(Scene& s) {
  // ruhiger Nachthimmel
  const Col bg = hex(0x0b1026);
  s.rrect(0, 0, W, H, 60, bg);
  s.glow(W / 2, PLAY_Y, 190, hex(0x2a2f7a), 0.35f);
  for (int i = 0; i < 18; i++) {
    const float x = 24 + std::fmod(i * 97.3f, W - 48), y = 20 + std::fmod(i * 61.7f, H - 40);
    if (std::fabs(y - PLAY_Y) < 90 && std::fabs(x - W / 2) < 150) continue;
    s.circle(x, y, 1.2f + (i % 3) * 0.4f, hex(0xe8ecff), 0.35f + 0.2f * std::sin(clock_ * 0.5f + i));
  }

  // oben: Schlafenszeit oder „Radio“
  if (locked_) {
    moon(s, W / 2 - 72, 46, 11, bg);
    s.text(fonts::body, "Schlafenszeit", W / 2 + 10, 32, hex(0xd8d0ff), 1, Align::Center);
  } else {
    s.text(fonts::small, "Radio", W / 2, 32, TXT_DIM, 1, Align::Center);
  }

  const int n = p_.count();
  if (!n) {
    s.text(fonts::title, p_.online() ? "Noch keine Sender" : "Radio schläft", W / 2, 150, TXT, 1, Align::Center);
    s.text(fonts::body, p_.online() ? "Eltern legen sie" : "Keine Verbindung", W / 2, 206, TXT_DIM, 1, Align::Center);
    s.text(fonts::body, p_.online() ? "im Portal an." : "zum Eltern-Portal.", W / 2, 236, TXT_DIM, 1, Align::Center);
    moon(s, W / 2, 330, 26, bg);
    return;
  }

  // Sendername (bis zwei Zeilen) und Art
  RadioSource src{};
  const int ci = p_.current();
  const bool has = p_.source(ci < 0 ? 0 : ci, src);
  if (has) {
    // Titelschrift, wenn der Name in zwei Zeilen passt (auch einzelne lange Wörter), sonst kleiner
    char lines[2][160];
    const fonts::Font* f = &fonts::title;
    int nl = wrapText(*f, src.name, 290, lines, 2);
    for (int i = 0; i < nl; i++) if (Scene::textW(*f, lines[i]) > 300) nl = -1;
    if (nl < 0) { f = &fonts::body; nl = wrapText(*f, src.name, 300, lines, 2); }
    if (nl < 0) nl = 0;
    const float y0 = (nl > 1 ? 64 : 84) - (f == &fonts::body ? -6 : 0);
    for (int i = 0; i < nl; i++) s.text(*f, lines[i], W / 2, y0 + i * f->lineH, TXT, 1, Align::Center);
    s.text(fonts::small, src.spotify ? "Spotify" : "Aufnahme", W / 2, y0 + nl * f->lineH + 2, TXT_DIM, 1, Align::Center);
  }

  // Wellen um den Knopf, wenn Ton läuft
  const bool playing = p_.playing();
  if (playing) {
    const float lv = p_.level();
    for (int k = 0; k < 3; k++) {
      const float u = std::fmod(clock_ * 0.35f + k / 3.f, 1.f);
      s.ring(PLAY_X, PLAY_Y, PLAY_R + 8 + u * 46, 2.5f, hex(0xb7a6ff), (1 - u) * (0.25f + 0.5f * lv));
    }
  }
  // Abspielknopf
  const float pr = PLAY_R * (pressed_ == B_PLAY && pressT_ > 0 ? 0.94f : 1.f);
  s.glow(PLAY_X, PLAY_Y, pr * 1.5f, PLAY_BOT, 0.3f, pr * 0.9f);
  s.ellipse(PLAY_X, PLAY_Y, pr, pr, PLAY_TOP, PLAY_BOT);
  if (playing && p_.buffering()) {   // lädt: kleiner Bogen dreht sich
    s.arc(PLAY_X, PLAY_Y, pr * 0.42f, 5, clock_ * 4, clock_ * 4 + 4, INK);
  } else if (playing) {   // Pause
    s.capsule(PLAY_X - 13, PLAY_Y - 18, PLAY_X - 13, PLAY_Y + 18, 7, INK);
    s.capsule(PLAY_X + 13, PLAY_Y - 18, PLAY_X + 13, PLAY_Y + 18, 7, INK);
  } else {
    tri(s, PLAY_X + 6, PLAY_Y, 24, 1, INK);
  }
  // Senderwahl
  if (n > 1)
    for (int d = -1; d <= 1; d += 2) {
      const float x = d < 0 ? PREV_X : NEXT_X;
      const bool pr2 = pressT_ > 0 && pressed_ == (d < 0 ? B_PREV : B_NEXT);
      s.circle(x, PLAY_Y, ARROW_R + 2, BTN_RIM);
      s.circle(x, PLAY_Y, ARROW_R, pr2 ? BTN_RIM : BTN);
      tri(s, x + d * 2, PLAY_Y, 11, float(d), TXT);
    }
  // nächstes Lied (nur Spotify)
  if (playing && has && src.spotify) {
    s.rrect(W / 2 - 84, SKIP_Y, 168, SKIP_H, SKIP_H / 2, pressT_ > 0 && pressed_ == B_SKIP ? BTN_RIM : BTN);
    s.text(fonts::small, "Nächstes Lied", W / 2, SKIP_Y + (SKIP_H - fonts::small.lineH) / 2 + 1, TXT, 1, Align::Center);
  }
  // Lautstärke: − Balken +
  for (int d = -1; d <= 1; d += 2) {
    const float x = d < 0 ? MINUS_X : PLUS_X;
    const bool pr3 = pressT_ > 0 && pressed_ == (d < 0 ? B_MINUS : B_PLUS);
    s.circle(x, VOL_Y, VOL_R + 2, BTN_RIM);
    s.circle(x, VOL_Y, VOL_R, pr3 ? BTN_RIM : BTN);
    s.capsule(x - 9, VOL_Y, x + 9, VOL_Y, 2.6f, TXT);
    if (d > 0) s.capsule(x, VOL_Y - 9, x, VOL_Y + 9, 2.6f, TXT);
  }
  const int mv = p_.maxVolume() > 0 ? p_.maxVolume() : 1, steps = 8;
  const float x0 = MINUS_X + 40, x1 = PLUS_X - 40;
  for (int i = 0; i < steps; i++) {
    const float x = x0 + (x1 - x0) * (i + 0.5f) / steps, h = 5 + i * 2.2f;
    const bool on = p_.volume() * steps > i * mv;
    s.capsule(x, VOL_Y + 10 - h, x, VOL_Y + 10, 3, on ? hex(0xb7a6ff) : hex(0x2e2e58));
  }
  // Schlaf-Timer oder Hinweis
  char b[64];
  p_.hint(b, sizeof b);
  if (b[0]) s.text(fonts::small, b, W / 2, 408, hex(0xffc9a0), 1, Align::Center);
  else if (playing && p_.sleepLeft() > 0) {
    snprintf(b, sizeof b, "Schlaf-Timer: noch %d min", int(std::ceil(p_.sleepLeft() / 60)));
    const float tw = Scene::textW(fonts::small, b);
    moon(s, W / 2 - tw / 2 - 14, 420, 8, bg);
    s.text(fonts::small, b, W / 2 + 6, 408, TXT_DIM, 1, Align::Center);
  } else if (!p_.online()) {
    s.text(fonts::small, "Keine Verbindung zum Portal", W / 2, 408, TXT_DIM, 1, Align::Center);
  }
}

}  // namespace dk
