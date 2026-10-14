#include "wakeup.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fonts.h"

namespace dk {

static constexpr float RAMP = 60, MAX_SECS = 600;   // s bis zur vollen Lautstärke, s bis zum Ende

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static Col mixc(Col a, Col b, float t) { return {uint8_t(a.r + (b.r - a.r) * t), uint8_t(a.g + (b.g - a.g) * t), uint8_t(a.b + (b.b - a.b) * t)}; }

void Wakeup::enter(uint32_t) {
  t_ = 0;
  nextNote_ = 0.5f;
  done_ = false;
  melody_ = true;
  if (std::strcmp(radio_.alarmTone, "melodie") != 0)   // Weckton ist ein Sender
    for (int i = 0; i < radio_.count(); i++) {
      RadioSource s;
      if (radio_.source(i, s) && !std::strcmp(s.id, radio_.alarmTone)) { radio_.select(i); radio_.play(); melody_ = false; }
    }
}

void Wakeup::restoreVolume() {
  const int snd = store_.get("snd", 0);
  synth_.setVolume(snd == 0 ? 0.f : snd == 1 ? 0.3f : 0.8f);   // wie App::applySound
}

void Wakeup::leave() {
  restoreVolume();
  if (!melody_) radio_.stop();
}

void Wakeup::tick(const TileInput& in) {
  t_ += in.dt;
  epoch_ = in.epoch;
  if (done_) return;
  if ((in.tap && t_ > 1) || t_ > MAX_SECS) { done_ = true; restoreVolume(); if (!melody_) radio_.stop(); requestGoto(GOTO_HOME); return; }
  if (!melody_) return;
  synth_.setVolume(0.15f + 0.55f * smooth(t_ / RAMP));   // Weckton auch bei „Ton aus“
  if (t_ >= nextNote_) {   // kleine Melodie: C-E-G-C, dann Pause
    static const float NOTES[5] = {523.3f, 659.3f, 784.0f, 1046.5f, 784.0f};
    for (int k = 0; k < 5; k++) {
      Synth::Voice v; v.wave = Synth::SINE; v.f0 = v.f1 = NOTES[k]; v.delay = 0.28f * k; v.dur = 0.5f; v.vol = 0.35f; v.attack = 0.03f;
      synth_.play(v);
    }
    nextNote_ = t_ + 5;
  }
}

void Wakeup::draw(Scene& s) {
  const float u = smooth(t_ / 20);   // Sonnenaufgang in 20 s
  const Col top = mixc(hex(0x0d1430), hex(0x5a8ad8), u), bot = mixc(hex(0x2a1840), hex(0xffb070), u);
  s.ellipse(W / 2, H / 2, 420, 380, top, bot);
  const float sy = H + 40 - 230 * u;
  s.glow(W / 2, sy, 200, hex(0xffd88a), 0.4f + 0.3f * u);
  s.circle(W / 2, sy, 58, hex(0xffe08a));
  for (int k = 0; k < 3; k++) {   // Wolken ziehen langsam
    const float cx = std::fmod(60 + k * 140 + t_ * (6 + 2 * k), W + 160) - 80, cy = 120 + k * 46;
    s.ellipse(cx, cy, 44, 16, hex(0xffffff), 0.25f + 0.25f * u);
    s.ellipse(cx + 22, cy - 9, 26, 14, hex(0xffffff), 0.25f + 0.25f * u);
  }
  if (epoch_ > 0) {
    char b[8];
    const int m = int((epoch_ / 60) % 1440);
    snprintf(b, sizeof b, "%d:%02d", m / 60, m % 60);
    s.text(fonts::big, b, W / 2, 168, hex(0xfff8ee), 1, Align::Center);
  }
  s.text(fonts::title, "Guten Morgen!", W / 2, 70, hex(0xfff8ee), smooth(t_ / 1.5f), Align::Center);
  s.text(fonts::body, "Tippen zum Aufstehen", W / 2, 400, hex(0x4a2410), (0.75f + 0.25f * std::sin(t_ * 2.5f)) * u, Align::Center);
}

}  // namespace dk
