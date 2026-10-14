// Kachel „Radio“ (Elternradio): Sender aus dem Eltern-Portal (eigene Aufnahmen, Spotify-Playlists).
// Großer Knopf zum Abspielen, Pfeile wechseln den Sender, − und + die Lautstärke (begrenzt durch die Eltern).
// Bei Sperre (Schlafenszeit) ist das die einzige Kachel; der Schlaf-Timer blendet langsam aus.
#pragma once
#include "radio_player.h"
#include "tile.h"

namespace dk {

class Radio : public Tile {
 public:
  explicit Radio(RadioPlayer& player) : p_(player) {}
  const char* title() const override { return "Radio"; }
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;

  // für den Simulator
  static constexpr float PLAY_X = W / 2, PLAY_Y = 236, PLAY_R = 56;
  static constexpr float PREV_X = 64, NEXT_X = W - 64, ARROW_R = 30;
  static constexpr float VOL_Y = 372, MINUS_X = 84, PLUS_X = W - 84, VOL_R = 24;
  static constexpr float SKIP_Y = 306, SKIP_H = 38;

 private:
  RadioPlayer& p_;
  float clock_ = 0, pressT_ = 0;
  int pressed_ = -1;   // zuletzt gedrückter Knopf (kurzes Aufleuchten)
  bool locked_ = false;
};

}  // namespace dk
