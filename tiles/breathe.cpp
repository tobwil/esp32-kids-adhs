#include "breathe.h"

#include <cmath>

#include "fonts.h"
#include "rewards.h"

namespace dk {

static const Col TXT = hex(0xf2eefa), TXT_DIM = hex(0x9a94b4), INK = hex(0x5a2238);
static const Col BAL_TOP = hex(0xffb8cc), BAL_BOT = hex(0xe8668e), CALM = hex(0x8fe3d0);
static constexpr float BX = W / 2, BY = 176, R_MIN = 52, R_MAX = 112;

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

float Breathe::level() const {
  if (phase_ != RUN || t_ < intro_) return 0;
  const float L = in_ + out_, u = std::fmod(t_ - intro_, L);
  return u < in_ ? 0.5f - 0.5f * std::cos(PIF * u / in_) : 0.5f + 0.5f * std::cos(PIF * (u - in_) / out_);
}

void Breathe::tick(const TileInput& in) {
  t_ += in.dt;
  clock_ += in.dt;
  switch (phase_) {
    case IDLE:
      if (in.tap) start();
      break;
    case RUN: {
      if (in.tap && t_ > 0.5f) { phase_ = IDLE; t_ = 0; requestGoto(GOTO_BACK); break; }   // abbrechen, ohne Vorwurf
      if (t_ < intro_) break;
      const float L = in_ + out_, k = t_ - intro_;
      round_ = int(k / L);
      if (round_ >= rounds_) { phase_ = DONE; t_ = 0; stars_ = starsAdd(store_); break; }
      // sanfter Ton zu Beginn jeder Hälfte (nur wenn der Ton an ist)
      const int half = int(k / L) * 2 + (std::fmod(k, L) >= in_ ? 1 : 0);
      if (half != lastHalf_) {
        lastHalf_ = half;
        Synth::Voice v; v.wave = Synth::SINE; v.hold = true; v.attack = 0.4f; v.vol = 0.05f;
        if (half & 1) { v.f0 = 440; v.f1 = 262; v.dur = out_ * 0.9f; }
        else { v.f0 = 330; v.f1 = 440; v.dur = in_ * 0.9f; }
        synth_.play(v);
      }
      break;
    }
    case DONE:
      if (t_ > 3.5f || (in.tap && t_ > 1)) { phase_ = IDLE; t_ = 0; requestGoto(GOTO_BACK); }
      break;
  }
}

// kleine Symbole neben dem Text
static void flower(Scene& s, float x, float y, float a) {
  for (int k = 0; k < 5; k++) s.circle(x + 7 * std::sin(k * TAU / 5), y - 7 * std::cos(k * TAU / 5), 5, hex(0xff9ec4), a);
  s.circle(x, y, 4, hex(0xffe08a), a);
}
static void candle(Scene& s, float x, float y, float a, float clock) {
  s.rrect(x - 5, y - 2, 10, 16, 2, hex(0xf2eefa), a);
  const float fl = 1 + 0.15f * std::sin(clock * 17);
  s.glow(x, y - 9, 11 * fl, hex(0xffb050), 0.8f * a);
  s.ellipse(x, y - 8, 3 * fl, 5.5f * fl, hex(0xffe08a), a);
}

void Breathe::draw(Scene& s) {
  const float b = level();
  const float r = R_MIN + (R_MAX - R_MIN) * b;
  const float bob = phase_ == IDLE ? 5 * std::sin(clock_ * 1.1f) : 0;
  const float x = BX + (phase_ == IDLE ? 6 * std::sin(clock_ * 0.37f) : 0), y = BY + bob;
  // Ruhe-Ring wächst mit
  if (phase_ == RUN) {
    const float a = smooth(t_ / 1.0f);
    s.ring(x, y, r + 22 + 10 * b, 3, CALM, (0.25f + 0.35f * b) * a);
  }
  // Ballon mit Knoten und kurzer Schnur
  const float rx = r * 0.9f, ry = r;
  s.glow(x, y, r * 1.4f, BAL_BOT, 0.18f, r * 0.85f);
  s.capsule(x, y + ry + 6, x + 4 * std::sin(clock_ * 2), y + ry + 34, 1.2f, hex(0xd8d0e8), 0.8f);
  s.ellipse(x, y + ry + 3, 6, 5, BAL_BOT);
  s.ellipse(x, y, rx, ry, BAL_TOP, BAL_BOT);
  s.ellipse(x - rx * 0.38f, y - ry * 0.45f, rx * 0.2f, ry * 0.13f, hex(0xffffff), 0.35f);
  // Gesicht: Augen zu, beim Ausatmen pustet der Mund
  const bool outPhase = phase_ == RUN && t_ >= intro_ && std::fmod(t_ - intro_, in_ + out_) >= in_;
  const float ex = rx * 0.3f, ey = y - ry * 0.05f, e = r * 0.11f;
  for (int sd = -1; sd <= 1; sd += 2) s.arc(x + sd * ex, ey, e, 3, PIF - 1.1f, PIF + 1.1f, INK);
  if (outPhase) s.ellipse(x, y + ry * 0.38f, r * 0.07f, r * 0.09f, INK);
  else s.arc(x, y + ry * 0.2f, r * 0.17f, 3, PIF - 0.9f, PIF + 0.9f, INK);
  s.ellipse(x - rx * 0.55f, y + ry * 0.18f, r * 0.12f, r * 0.07f, hex(0xff8fb8), 0.5f);
  s.ellipse(x + rx * 0.55f, y + ry * 0.18f, r * 0.12f, r * 0.07f, hex(0xff8fb8), 0.5f);

  const float ty = 330;
  switch (phase_) {
    case IDLE: {
      const float k = std::fmod(clock_, 20.f), a = smooth(k / 0.8f) * (1 - smooth((k - 14) / 0.8f));
      s.text(fonts::title, "Tippen zum Atmen", W / 2, ty, TXT, a, Align::Center);
      drawStarCount(s, W / 2, 400, starsGet(store_), 0.7f * a);
      break;
    }
    case RUN: {
      if (t_ < intro_) {
        s.text(fonts::title, "Wir atmen zusammen", W / 2, ty, TXT, smooth(t_ / 0.5f) * (1 - smooth((t_ - intro_ + 0.4f) / 0.4f)), Align::Center);
        break;
      }
      const float L = in_ + out_, u = std::fmod(t_ - intro_, L);
      const bool inhale = u < in_;
      const float pu = inhale ? u : u - in_, pl = inhale ? in_ : out_;
      const float a = smooth(pu / 0.5f) * (1 - smooth((pu - pl + 0.5f) / 0.5f));
      s.text(fonts::title, inhale ? "Einatmen" : "Ausatmen", W / 2, ty, TXT, a, Align::Center);
      const char* hint = inhale ? "Riech an der Blume" : "Puste die Kerze aus";
      const float hw = Scene::textW(fonts::body, hint);
      s.text(fonts::body, hint, W / 2 + 14, ty + 42, TXT_DIM, a, Align::Center);
      if (inhale) flower(s, W / 2 - hw / 2 - 12, ty + 56, a);
      else candle(s, W / 2 - hw / 2 - 12, ty + 58, a, clock_);
      for (int i = 0; i < rounds_; i++)
        s.circle(W / 2 + (i - (rounds_ - 1) * 0.5f) * 18, 420, 4, i <= round_ ? CALM : hex(0x3a3550), 0.8f);
      break;
    }
    case DONE: {
      const float a = smooth(t_ / 0.5f);
      s.text(fonts::title, "Super gemacht!", W / 2, ty, TXT, a, Align::Center);
      // neuer Stern dreht sich ein und landet neben der Zahl
      const float u = smooth((t_ - 0.3f) / 0.9f);
      s.star(W / 2, ty + 72, 10 + 22 * std::sin(u * PIF) + 12 * u, hex(0xffd166), u * (1 - smooth((t_ - 1.4f) / 0.3f)), (1 - u) * 6);
      drawStarCount(s, W / 2, ty + 56, stars_, smooth((t_ - 1.4f) / 0.3f));
      break;
    }
  }
}

}  // namespace dk
