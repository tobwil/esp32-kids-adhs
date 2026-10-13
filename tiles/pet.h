// Grummel als sanftes Haustier (wie ein Tamagotchi, aber ohne Druck):
// schlüpft aus einem Ei, wächst durch Pflege (Baby → Kind → Groß) und hat drei Bedürfnisse, die langsam sinken.
// Er stirbt nie, wird nie krank und macht kein schlechtes Gewissen: Fehlt ihm etwas, fragt er nur freundlich.
// Andere Apps melden Pflege über petEvent() (Spielen, Ruhe-Übungen), das Dashboard wertet sie aus.
#pragma once
#include "input.h"

namespace dk {

struct Pet {
  enum Stage { EGG, BABY, KID, BIG };
  enum Need { FOOD, CUDDLE, FUN, N_NEEDS };
  static constexpr int GROW_KID = 12, GROW_BIG = 40;   // Pflegepunkte bis zur nächsten Stufe

  Stage stage = EGG;
  int growth = 0;          // Pflegepunkte
  float need[N_NEEDS] = {0.7f, 0.7f, 0.7f};
  float warmth = 0;        // Ei: wie warm gestreichelt (1 = schlüpft)

  void load(Store& s);
  void save(Store& s) const;
  void update(float dt);                 // Bedürfnisse sinken langsam (Stunden)
  bool applyEvents(Store& s);            // Ereignisse anderer Apps übernehmen; true, wenn es etwas gab
  bool addGrowth(int pts);               // true, wenn Grummel dabei eine Stufe gewachsen ist
  float size() const;                    // Radius für die Stufe
  static int accessory(int stars);       // 0 nichts, 1 Schleife (10 Sterne), 2 Blume (25), 3 Krone (50)
};

// Pflege-Ereignis melden (aus anderen Apps): "pe_play" (Spielen), "pe_calm" (Atmen, Grummel, Gefühle)
inline void petEvent(Store& s, const char* key) { s.set(key, s.get(key, 0) + 1); }

}  // namespace dk
