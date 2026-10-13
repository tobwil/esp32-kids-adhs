// Kachel „Atmen“: ein Ballon atmet mit. Einatmen („Riech an der Blume“) bläst ihn auf, Ausatmen („Puste die Kerze aus“)
// lässt ihn schrumpfen. Ein paar Runden, dann gibt es einen Stern und es geht zurück zum Check-in.
#pragma once
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Breathe : public Tile {
 public:
  Breathe(Synth& synth, Store& store) : synth_(synth), store_(store) {}
  const char* title() const override { return "Atmen"; }
  void enter(uint32_t) override { phase_ = IDLE; t_ = 0; }
  void arrive(bool returning) override { if (!returning) start(); }   // geöffnet: gleich loslegen
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != IDLE; }

  enum Phase { IDLE, RUN, DONE };
  Phase phase() const { return phase_; }

 private:
  void start() { phase_ = RUN; t_ = 0; round_ = 0; lastHalf_ = -1; }
  float level() const;   // 0..1 wie voll der Ballon ist

  Synth& synth_;
  Store& store_;
  Phase phase_ = IDLE;
  float t_ = 0, clock_ = 0;
  int round_ = 0, lastHalf_ = -1;
  int stars_ = 0;
  // später über die Elternseite einstellbar
  int rounds_ = 4;
  float in_ = 4, out_ = 5, intro_ = 2;
};

}  // namespace dk
