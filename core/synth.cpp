#include "synth.h"

#include <cmath>

namespace dk {

static constexpr float TWO_PI = 6.2831853f;

uint32_t Synth::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return seed_;
}

void Synth::play(const Voice& v) {
  std::lock_guard<std::mutex> l(m_);
  if (master_ <= 0) return;
  int best = 0;
  float bestLeft = 1e9f;
  for (int i = 0; i < 14; i++) {
    if (!s_[i].on) { best = i; break; }
    float left = s_[i].v.delay + s_[i].v.dur - s_[i].t;
    if (left < bestLeft) { bestLeft = left; best = i; }
  }
  Slot& s = s_[best];
  s = Slot{};
  s.v = v;
  s.on = true;
  s.t = -v.delay;
}

float Synth::sample(Slot& sl, float dt) {
  Voice& v = sl.v;
  sl.t += dt;
  if (sl.t < 0) return 0;
  if (sl.t >= v.dur) { sl.on = false; return 0; }
  const float k = sl.t / v.dur;
  float f = v.f0 == v.f1 ? v.f0 : v.f0 * std::pow(v.f1 / v.f0, k);
  if (v.lfoD > 0) f += v.lfoD * std::sin(TWO_PI * v.lfoF * sl.t);
  float s;
  if (v.wave == NOISE) {
    s = float(int32_t(rnd())) / 2147483648.f;
  } else {
    sl.ph += f * dt;
    sl.ph -= std::floor(sl.ph);
    switch (v.wave) {
      case SINE: s = std::sin(TWO_PI * sl.ph); break;
      case SQUARE: s = sl.ph < 0.5f ? 0.8f : -0.8f; break;
      case TRI: s = 4.f * std::fabs(sl.ph - 0.5f) - 1.f; break;
      default: s = 2.f * sl.ph - 1.f; break;
    }
  }
  if (v.filt != NONE) {
    float fc = v.cut0 == v.cut1 ? v.cut0 : v.cut0 * std::pow(v.cut1 / v.cut0, k);
    if (fc > 2400) fc = 2400;   // Chamberlin-Filter bleibt bei 16 kHz bis etwa hier stabil
    const float F = 2.f * std::sin(3.14159265f * fc / RATE), damp = 1.f / v.q;
    sl.lp += F * sl.bp;
    const float hp = s - sl.lp - damp * sl.bp;
    sl.bp += F * hp;
    s = v.filt == LP ? sl.lp : v.filt == BP ? sl.bp : hp;
  }
  float g = sl.t < v.attack ? sl.t / v.attack : 1.f;
  if (v.hold) { if (v.dur - sl.t < 0.03f) g *= (v.dur - sl.t) / 0.03f; }
  else g *= std::exp(-5.f * (sl.t - v.attack > 0 ? sl.t - v.attack : 0) / v.dur);
  if (v.amD > 0) g *= 1.f - v.amD * (0.5f + 0.5f * std::sin(TWO_PI * v.amF * sl.t));
  return s * g * v.vol;
}

void Synth::render(int16_t* out, int n) {
  std::lock_guard<std::mutex> l(m_);
  const float dt = 1.f / RATE;
  for (int i = 0; i < n; i++) {
    float s = 0;
    for (auto& sl : s_)
      if (sl.on) s += sample(sl, dt);
    s *= master_;
    s = s * (27.f + s * s) / (27.f + 9.f * s * s);   // weiche Begrenzung
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    out[i] = int16_t(s * 28000.f);
  }
}

}  // namespace dk
