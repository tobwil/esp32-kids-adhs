// Kachel „Pferde-Album“: Sammelkarten mit echtem Foto und echten Infos.
// Alle drei Sterne aus den Übungen gibt es eine neue Karte (verdeckt, „Tippen zum Aufdecken“), nie doppelt.
// Vorderseite: Foto, Name, Typ, Herkunft, Größe. Ein Tipp dreht um: Farben, Wesen, „Wusstest du?“, Bildnachweis.
#pragma once
#include <vector>

#include "images.h"
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

struct HorseCard {
  int id;
  const char *name, *typ, *herkunft, *groesse, *farben, *wesen, *besonders, *foto;
};

class Album : public Tile {
 public:
  static constexpr int STARS_PER_CARD = 3;

  Album(Synth& synth, Store& store, uint32_t seed);
  void setImages(ImageStore* img) { img_ = img; }
  bool load(const char* text);   // Kartentext (siehe tools/horses.py), wird kopiert
  const char* title() const override { return "Pferde-Album"; }
  void enter(uint32_t ms) override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != GRID; }

  int count() const { return int(cards_.size()); }
  int owned() const { return opened_; }
  int pending() const;
  // für den Simulator
  enum Phase { GRID, FRONT, BACK, WRAPPED, REVEAL };
  Phase phase() const { return phase_; }
  static void cellRect(int slot, float& x, float& y, float& w, float& h);

 private:
  void shuffle();
  int cardAt(int slot) const { return order_[slot]; }   // Sammelreihenfolge → Kartenindex
  void drawGrid(Scene& s);
  void drawFront(Scene& s, int card, float a);
  void drawBack(Scene& s, int card, float a);
  void drawWrapped(Scene& s, float a);

  Synth& synth_;
  Store& store_;
  ImageStore* img_ = nullptr;
  uint32_t seed_;
  std::vector<char> text_;
  std::vector<HorseCard> cards_;
  std::vector<int> order_;   // gemischte Reihenfolge, in der die Karten freigeschaltet werden
  int opened_ = 0;
  Phase phase_ = GRID;
  int page_ = 0, card_ = 0;
  float t_ = 0, clock_ = 0, hintT_ = 0;
};

}  // namespace dk
