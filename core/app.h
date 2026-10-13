// Launcher: Kacheln nebeneinander, Wischen nach links/rechts wechselt, langer Druck oder BOOT öffnet die Einstellungen.
// Kümmert sich außerdem um Einbrennschutz (Bild wandert langsam, Dimmen und Abschalten bei Ruhe) und den Ton.
#pragma once
#include <vector>

#include "input.h"
#include "motion.h"
#include "render.h"
#include "synth.h"
#include "tile.h"

namespace dk {

class App {
 public:
  App(Synth& synth, Store& store);
  void add(Tile* t) { tiles_.push_back(t); }
  void begin(uint32_t ms);
  void tick(const Input& in);
  void draw(Scene& s);
  int brightness() const { return bright_; }   // gewünschte Panel-Helligkeit 0..255
  bool screenOff() const { return off_; }
  const char* debug();
  void show(int i) { setCurrent(i); pos_ = target_ = float(cur_); }   // direkt zu einer Kachel (Test)
  void setHome(int i) { home_ = i; }                  // Startkachel: dort beginnt das Gerät und dorthin geht es nach dem Aufwachen
  void setSettingsHold(uint32_t ms) { holdMs_ = ms; } // so lange drücken für die Einstellungen (Kindersicherung)
  void setShowTitles(bool on) { showTitles_ = on; }   // Kachelnamen nach dem Wechsel kurz einblenden
  // Ruhe: nach dimMs dunkler, nach offMs schwarz. startOff: nach dem Einschalten schwarz bis zum ersten Tippen.
  // wakeOnMotion: Klopfen/Schütteln weckt auch (sonst nur Berührung und BOOT)
  void setPower(uint32_t dimMs, uint32_t offMs, bool startOff, bool wakeOnMotion) { dimMs_ = dimMs; offMs_ = offMs; startOff_ = startOff; wakeOnMotion_ = wakeOnMotion; }
  Motion motion;

 private:
  void setCurrent(int i);
  void slideTo(int i);
  void wake();
  void openSettings();
  void settingsTap(float x, float y);
  void drawSettings(Scene& s);
  int wrap(int i) const { int n = int(tiles_.size()); return ((i % n) + n) % n; }
  int settingsItems() const;
  void applySound();

  Synth& synth_;
  Store& store_;
  std::vector<Tile*> tiles_;
  int cur_ = 0;
  float pos_ = 0, target_ = 0;   // Seitenposition (gleitend), Ziel beim Einrasten
  bool sliding_ = false;

  // Touch
  enum Mode { NONE, PENDING, HDRAG, DONE } mode_ = NONE;
  bool touching_ = false;
  float sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0, vx_ = 0, touchPeak_ = 0;
  uint32_t sMs_ = 0, lMs_ = 0, releaseMs_ = 0, shakeMs_ = 0;
  float dragStartPos_ = 0;
  bool bootWas_ = false;

  // Anzeige
  uint32_t ms_ = 0, lastAct_ = 0;
  float dt_ = 0;
  float dotsT_ = 0, titleT_ = 0;
  bool settings_ = false;
  float settingsA_ = 0;
  int sound_ = 0;   // 0 aus, 1 leise, 2 an
  int bright_ = 200;
  bool off_ = false, dim_ = false;
  int home_ = -1;
  uint32_t holdMs_ = 700;
  int arriveAt_ = -1;
  bool showTitles_ = true;
  uint32_t dimMs_ = 120000, offMs_ = 600000;
  bool startOff_ = false, wakeOnMotion_ = true;
  float orbX_ = 0, orbY_ = 0;
  char dbg_[160];
};

}  // namespace dk
