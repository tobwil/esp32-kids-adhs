// Sterne für das Benutzen der Ruhe-Werkzeuge (nicht fürs „Bravsein“), gemeinsam für alle Kacheln.
#pragma once
#include <cstdio>

#include "fonts.h"
#include "input.h"
#include "render.h"

namespace dk {

inline int starsGet(Store& s) { return s.get("sterne", 0); }
inline int starsAdd(Store& s) { const int n = s.get("sterne", 0) + 1; s.set("sterne", n); return n; }

// Stern-Symbol mit Zahl, mittig um x
inline void drawStarCount(Scene& s, float x, float y, int n, float a = 1) {
  char b[16];
  snprintf(b, sizeof b, "%d", n);
  const float w = 26 + Scene::textW(fonts::body, b);
  s.star(x - w / 2 + 11, y + 15, 11, hex(0xffd166), a);
  s.text(fonts::body, b, x - w / 2 + 26, y, hex(0xffe9a8), a);
}

}  // namespace dk
