// Kachel „Post“: Sprachnachrichten mit Mama, Papa, … (nur Kontakte aus dem Eltern-Portal).
// Oben die letzten Nachrichten an das Kind (antippen spielt sie ab, das Radio pausiert so lange),
// unten große Knöpfe für die Kontakte. Aufnehmen: großer Knopf, höchstens 30 s, dann „Senden“ oder „Nochmal“.
#pragma once
#include "messages.h"
#include "radio_player.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Post : public Tile {
 public:
  Post(Synth& synth, Messages& m, RadioPlayer& r) : synth_(synth), m_(m), r_(r) {}
  const char* title() const override { return "Post"; }
  void enter(uint32_t) override { phase_ = LIST; t_ = 0; }
  void leave() override { if (m_.recording()) m_.stopRecording(); }
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != LIST || r_.clipPlaying(); }

  enum Phase { LIST, RECORD, SENT };
  Phase phase() const { return phase_; }
  // für den Simulator
  static void rowRect(int i, float& x, float& y, float& w, float& h);
  static void contactPos(int i, int n, float& x, float& y);
  static constexpr float REC_X = W / 2, REC_Y = 214, REC_R = 64, ACT_Y = 362, ACT_DX = 84, ACT_R = 34;

 private:
  void chime(bool up);

  Synth& synth_;
  Messages& m_;
  RadioPlayer& r_;
  Phase phase_ = LIST;
  float t_ = 0, clock_ = 0, voice_ = 0;
  Contact to_{};
  char playing_[16] = "";
  bool offline_ = false;
};

}  // namespace dk
