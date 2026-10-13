// Gezeichnetes Pferd (Vektorformen), für Silhouetten im Pferde-Album und als Platzhalter ohne Foto.
#pragma once
#include <cstdint>

#include "render.h"

namespace dk {

enum class Coat : uint8_t { BROWN, CHESTNUT, BLACK, GREY, PINTO, SPOTTED, DUN, PALOMINO };

struct Horse {
  const char* name;
  int heightCm;
  Coat coat;
  uint32_t body, mane;  // Fellfarbe und Farbe von Mähne/Schweif
  bool feathers;        // Behang an den Hufen
};

// Pferd von der Seite (Blick nach rechts), Fußpunkt (x, y) in der Mitte zwischen den Hufen.
// scale 1 ≈ 120 px Breite; anim 0..1 schwingt Schweif und Kopf.
void drawHorse(Scene& s, const Horse& h, float x, float y, float scale, float anim, float a = 1);

}  // namespace dk
