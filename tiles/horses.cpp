#include "horses.h"

#include <cmath>

namespace dk {

void drawHorse(Scene& s, const Horse& h, float x, float y, float k, float anim, float a) {
  const Col body = hex(h.body), dark = scale(body, 0.72f), mane = hex(h.mane), hoof = hex(0x241a16);
  const float bx = x - 4 * k, by = y - 60 * k;
  const float swing = std::sin(anim * TAU), nod = 2 * k * std::sin(anim * TAU * 0.5f + 1);
  // Beine hinten (dunkler), Schweif
  s.capsule(bx + 22 * k, by + 10 * k, bx + 24 * k, y - 4 * k, 5.5f * k, dark, a);
  s.capsule(bx - 38 * k, by + 8 * k, bx - 40 * k, y - 4 * k, 5.5f * k, dark, a);
  s.capsule(bx - 44 * k, by - 12 * k, bx - 58 * k + swing * 5 * k, by + 28 * k, 6 * k, mane, a);
  // Körper und Hals
  s.ellipse(bx, by, 46 * k, 22 * k, body, scale(body, 0.85f), a);
  s.capsule(bx + 30 * k, by - 4 * k, bx + 52 * k, by - 30 * k, 14 * k, body, a);
  // Fellzeichnung
  const Col white = hex(0xf2eee8);
  if (h.coat == Coat::PINTO) {
    s.ellipse(bx - 12 * k, by - 4 * k, 17 * k, 13 * k, white, a);
    s.ellipse(bx + 20 * k, by + 8 * k, 9 * k, 7 * k, white, a);
    s.ellipse(bx + 38 * k, by - 22 * k, 6 * k, 8 * k, white, a);
  } else if (h.coat == Coat::SPOTTED) {
    static const float SX[7] = {-34, -26, -18, -30, -10, -22, -38}, SY[7] = {-8, 2, -12, 10, -2, 12, 4};
    for (int i = 0; i < 7; i++) s.circle(bx + SX[i] * k, by + SY[i] * k, 3.2f * k, hex(0x4a3628), a);
  } else if (h.coat == Coat::GREY) {
    for (int i = 0; i < 5; i++) s.circle(bx - 20 * k + i * 9 * k, by + (i & 1 ? 4 : -6) * k, 4 * k, scale(body, 1.06f), 0.6f * a);
  } else if (h.coat == Coat::DUN) {
    s.capsule(bx - 40 * k, by - 19 * k, bx + 26 * k, by - 20 * k, 1.6f * k, hex(0x3a2a1a), 0.8f * a);   // Aalstrich
  }
  // Beine vorne
  s.capsule(bx + 30 * k, by + 12 * k, bx + 32 * k, y - 4 * k, 5.8f * k, body, a);
  s.capsule(bx - 30 * k, by + 10 * k, bx - 30 * k, y - 4 * k, 5.8f * k, body, a);
  if (h.coat == Coat::PINTO) s.capsule(bx + 30 * k, y - 18 * k, bx + 32 * k, y - 4 * k, 4.8f * k, white, a);   // weiße Socke
  // Hufe, bei Kaltblütern mit Behang
  const float hx[4] = {bx + 24 * k, bx - 40 * k, bx + 32 * k, bx - 30 * k};
  for (int i = 0; i < 4; i++) {
    if (h.feathers) s.ellipse(hx[i], y - 6 * k, 7.5f * k, 5 * k, hex(0xe8e0d6), a);
    s.ellipse(hx[i], y - 2 * k, 5.5f * k, 3 * k, hoof, a);
  }
  // Mähne, Kopf, Ohr, Auge
  s.capsule(bx + 46 * k, by - 40 * k + nod, bx + 28 * k, by - 12 * k, 5.5f * k, mane, a);
  if (h.coat == Coat::DUN && h.mane == 0xf4ecd8) s.capsule(bx + 45 * k, by - 39 * k + nod, bx + 29 * k, by - 14 * k, 1.8f * k, hex(0x2a1e14), a);   // Fjord: dunkler Streifen
  s.capsule(bx + 52 * k, by - 38 * k + nod, bx + 76 * k, by - 22 * k + nod, 9.5f * k, body, a);
  s.circle(bx + 77 * k, by - 20 * k + nod, 7.5f * k, scale(body, 0.8f), a);
  s.capsule(bx + 50 * k, by - 46 * k + nod, bx + 50 * k, by - 56 * k + nod, 3.2f * k, body, a);
  s.capsule(bx + 56 * k, by - 45 * k + nod, bx + 50 * k, by - 40 * k + nod, 3.5f * k, mane, a);   // Stirnschopf
  s.circle(bx + 60 * k, by - 35 * k + nod, 2.2f * k, hex(0x100a08), a);
  s.circle(bx + 77 * k, by - 18 * k + nod, 1.2f * k, hex(0x100a08), 0.8f * a);
}

}  // namespace dk
