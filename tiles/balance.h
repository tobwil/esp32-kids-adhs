// Kachel „Kugel ins Loch“: durch Neigen die Kugeln ins Loch rollen. Ruhig, ohne Zeitdruck und ohne Verlieren.
// Zu Beginn jeder Runde gilt die aktuelle Haltung als „gerade“, damit man das Gerät auch schräg halten kann.
// Nach ein paar Runden schlägt das Spiel eine Pause vor und schickt zurück zur Startkachel.
#pragma once
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class Balance : public Tile {
 public:
  Balance(Synth& synth, Store& store);
  const char* title() const override { return "Kugel ins Loch"; }
  void enter(uint32_t ms) override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override { return phase_ != PAUSE; }
  bool claimsDrag() const override { return !tilt_; }
  int settingsCount() const override { return 3; }
  void settingText(int i, char* buf, int n) const override;
  bool settingTap(int i) override;

  enum Phase { START, PLAY, DONE, PAUSE };
  static constexpr int MAX_BALLS = 3, MAX_HOLES = 2, MAX_PEGS = 6;
  // für den Simulator (Autopilot)
  Phase phase() const { return phase_; }
  int level() const { return level_; }
  bool ballPos(int i, float& x, float& y) const;
  void holePos(int i, float& x, float& y) const;

 private:
  struct Ball { float x, y, vx, vy; bool in; float sink; int hole; };
  void startLevel();
  void physics(float ax, float ay, float dt);
  void bump(float v);

  Synth& synth_;
  Store& store_;
  Phase phase_ = START;
  float t_ = 0, clock_ = 0;
  int level_ = 0, inSession_ = 0, stars_ = 0;
  float baseX_ = 0, baseY_ = 0, bumpCd_ = 0;
  float gx_ = 0, gy_ = 0;   // aktuelle Rollrichtung (für den Schatten)
  Ball balls_[MAX_BALLS];
  int nBalls_ = 0;
  int dir_ = 0;             // Einstellung: 0 normal, 1 links/rechts gespiegelt, 2 oben/unten gespiegelt, 3 beides
  bool swap_ = false;       // Achsen tauschen (falls die IMU gedreht verbaut ist)
  bool tilt_ = false;       // Steuerung: Neigen (IMU) oder Finger als Joystick (Standard, die IMU dieser Platine ist defekt)
  bool finger_ = false;
  float fx_ = 0, fy_ = 0;
};

}  // namespace dk
