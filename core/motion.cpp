#include "motion.h"

#include <cmath>

namespace dk {

void Motion::feed(const AccelSample* s, int n, bool touchBusy) {
  nKnocks = 0;
  framePeak = 0;
  for (int i = 0; i < n; i++) {
    const AccelSample& a = s[i];
    if (!init_) { gx_ = tiltX = a.x; gy_ = tiltY = a.y; gz_ = tiltZ = a.z; lastMs_ = a.ms; init_ = true; }
    float dt = (a.ms - lastMs_) * 0.001f;
    if (dt <= 0) dt = 0.002f;
    if (dt > 0.1f) dt = 0.1f;
    lastMs_ = a.ms;

    // Schwerkraft langsam nachführen, außer mitten in einem Schlag
    if (!burst_) {
      const float k = dt / 0.4f;
      gx_ += (a.x - gx_) * k; gy_ += (a.y - gy_) * k; gz_ += (a.z - gz_) * k;
    }
    {
      const float k = dt / 0.05f > 1 ? 1 : dt / 0.05f;
      tiltX += (a.x - tiltX) * k; tiltY += (a.y - tiltY) * k; tiltZ += (a.z - tiltZ) * k;
    }
    const float hx = a.x - gx_, hy = a.y - gy_, hz = a.z - gz_;
    const float m = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (m > framePeak) framePeak = m;

    // Energie für das Schütteln (~0,25 s) und Grundunruhe (~2 s, ohne Schläge)
    energy_ += (m * m - energy_) * (dt / 0.25f);
    if (!burst_ && m < threshold) noise += (m - noise) * (dt / 2.f);

    const float rms = std::sqrt(energy_);
    if (!shaking) {
      if (rms > 0.4f) {
        if (!shakeSince_) shakeSince_ = a.ms | 1;
        if (a.ms - shakeSince_ >= 150) { shaking = true; quietSince_ = 0; }
      } else shakeSince_ = 0;
    } else if (rms < 0.25f) {
      if (!quietSince_) quietSince_ = a.ms | 1;
      if (a.ms - quietSince_ >= 300) { shaking = false; shakeSince_ = 0; }
    } else quietSince_ = 0;
    shake = shaking ? std::fmax(0.15f, std::fmin(1.f, (rms - 0.25f) / 1.0f)) : 0;

    // Klopfen: Schwelle passt sich an, wenn der Tisch dauernd wackelt (Tippen auf der Tastatur)
    const float thr = std::fmax(threshold, noise * 4.f);
    if (!burst_) {
      if (m > thr && a.ms - lastKnock_ > 90 && !touchBusy && !shaking) {
        burst_ = true;
        burstStart_ = a.ms;
        burstPeak_ = m; bx_ = hx; by_ = hy;
      }
    } else {
      if (m > burstPeak_) { burstPeak_ = m; bx_ = hx; by_ = hy; }
      if (a.ms - burstStart_ >= 25) {
        burst_ = false;
        lastKnock_ = a.ms;
        if (nKnocks < 8 && !touchBusy && !shaking) {
          Knock& k = knocks[nKnocks++];
          k.peak = burstPeak_;
          k.strength = std::fmin(1.f, (burstPeak_ - thr) / (3.f * thr));
          const float hor = std::sqrt(bx_ * bx_ + by_ * by_);
          k.dirX = hor > 0.3f * burstPeak_ ? bx_ / hor : 0;
          k.dirY = hor > 0.3f * burstPeak_ ? by_ / hor : 0;
          k.ms = a.ms;
          totalKnocks++;
        }
      }
    }
  }
}

}  // namespace dk
