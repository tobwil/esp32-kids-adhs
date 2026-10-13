// Bilder für Kacheln (Pferdekarten): die Plattform liefert sie, auf dem Gerät per JPEG aus dem Flash,
// im Simulator als Rohdaten. Ein leeres Image heißt „kein Bild“, dann zeichnet die Kachel einen Platzhalter.
#pragma once
#include "render.h"

namespace dk {

struct ImageStore {
  virtual Image card(int id) = 0;    // 328 × 196
  virtual Image thumb(int id) = 0;   // 164 × 98
  virtual void reload() {}           // nach einem Upload: Zwischenspeicher verwerfen
  virtual ~ImageStore() = default;
};

constexpr int CARD_W = 328, CARD_H = 196, THUMB_W = 164, THUMB_H = 98;

}  // namespace dk
