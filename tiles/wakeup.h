// Kachel „Wecker“: erscheint zur Weckzeit (auch bei Sperre), Sonnenaufgang mit Uhr und „Guten Morgen!“.
// Ton: eine sanfte Melodie, die langsam lauter wird (auch wenn der Ton sonst aus ist), oder ein Radiosender.
// Antippen beendet den Wecker; nach 10 min hört er von selbst auf.
#pragma once
#include "input.h"
#include "radio_player.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Wakeup : public Tile {
 public:
  Wakeup(Synth& synth, Store& store, RadioPlayer& radio) : synth_(synth), store_(store), radio_(radio) {}
  const char* title() const override { return "Wecker"; }
  void enter(uint32_t) override;
  void leave() override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return true; }

 private:
  void restoreVolume();

  Synth& synth_;
  Store& store_;
  RadioPlayer& radio_;
  float t_ = 0, nextNote_ = 0;
  int64_t epoch_ = 0;
  bool melody_ = true, done_ = false;
};

}  // namespace dk
