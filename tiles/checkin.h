// Startkachel „Wie fühlt sich dein Bauch an?“: fünf Gesichter zum Antippen.
// Je nach Gefühl schickt sie zum passenden Werkzeug (Wut → Grummel, kribbelig/traurig/müde → Atmen)
// und fragt nach der Übung noch einmal nach. Jeder Check-in landet mit Uhrzeit im Verlauf (für die Elternseite).
#pragma once
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class CheckIn : public Tile {
 public:
  enum Feeling { WUT, KRIBBELIG, TRAURIG, MUEDE, FROH, N_FEELINGS };

  CheckIn(Synth& synth, Store& store);
  void setTargets(int grummel, int breathe) { grummelTile_ = grummel; breatheTile_ = breathe; }
  const char* title() const override { return "Wie geht's dir?"; }
  void enter(uint32_t ms) override;
  void arrive(bool returning) override { again_ = returning; }   // zurück von einer Übung: „… jetzt an?“
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return chosen_ >= 0; }

  // für den Simulator
  int chosen() const { return chosen_; }
  static void faceCenter(int i, float& x, float& y);

 private:
  void log(int feeling, int64_t epoch);

  Synth& synth_;
  Store& store_;
  int grummelTile_ = -1, breatheTile_ = -1;
  int chosen_ = -1;
  bool again_ = false;
  float t_ = 0, clock_ = 0, idle_ = 0;
  int64_t epoch_ = 0;
};

// ein Gesicht zeichnen (auch für andere Kacheln)
void drawFace(Scene& s, int feeling, float x, float y, float r, float clock, float a = 1);

}  // namespace dk
