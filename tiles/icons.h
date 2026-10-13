// Kleine Symbole aus Vektorformen, gemeinsam für Dashboard und Rätsel.
#pragma once
#include "render.h"

namespace dk {

inline void heart(Scene& s, float x, float y, float r, Col c, float a) {
  s.circle(x - r * 0.5f, y - r * 0.2f, r * 0.55f, c, a);
  s.circle(x + r * 0.5f, y - r * 0.2f, r * 0.55f, c, a);
  s.capsule(x - r * 0.9f, y + r * 0.05f, x, y + r * 0.95f, r * 0.28f, c, a);
  s.capsule(x + r * 0.9f, y + r * 0.05f, x, y + r * 0.95f, r * 0.28f, c, a);
  s.circle(x, y + r * 0.25f, r * 0.5f, c, a);
}
inline void apple(Scene& s, float x, float y, float r, float a) {
  s.circle(x - r * 0.3f, y + r * 0.1f, r * 0.62f, hex(0xff6b5a), a);
  s.circle(x + r * 0.3f, y + r * 0.1f, r * 0.62f, hex(0xff6b5a), a);
  s.circle(x - r * 0.35f, y - r * 0.1f, r * 0.2f, hex(0xffb0a0), 0.8f * a);
  s.capsule(x, y - r * 0.45f, x + r * 0.12f, y - r * 0.85f, r * 0.1f, hex(0x6a4020), a);
  s.ellipse(x + r * 0.32f, y - r * 0.7f, r * 0.26f, r * 0.13f, hex(0x6ad07a), a);
}
inline void ball(Scene& s, float x, float y, float r, float a) {
  s.circle(x, y, r, hex(0x5fd8c8), a);
  s.arc(x, y, r * 0.62f, r * 0.22f, -0.9f, 0.9f, hex(0xffffff), 0.8f * a);
  s.arc(x, y, r * 0.62f, r * 0.22f, PIF - 0.9f, PIF + 0.9f, hex(0xffffff), 0.8f * a);
}
inline void starIcon(Scene& s, float x, float y, float r, float a) { s.star(x, y, r, hex(0xffd166), a); }

}  // namespace dk
