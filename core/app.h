// Launcher: ein Dashboard als Startseite, die Funktionen sind Apps, die bildschirmfüllend öffnen.
// Wischen nach oben/unten blättert durch die Apps (Reihenfolge wie hinzugefügt, das Dashboard steht vorn),
// Wischen nach links oder rechts schließt die App, ebenso das ✕ oben rechts. Apps können andere Apps öffnen (Check-in → Grummel) und kehren mit
// GOTO_BACK dorthin zurück. Langer Druck oder BOOT öffnet die Einstellungen.
// Außerdem: Einbrennschutz (Bild wandert langsam), Dimmen/Abschalten bei Ruhe, Ton.
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
  void add(Tile* t) { tiles_.push_back(t); back_.push_back(-1); }
  void begin(uint32_t ms);
  void tick(const Input& in);
  void draw(Scene& s);
  int brightness() const { return bright_; }   // gewünschte Panel-Helligkeit 0..255
  bool screenOff() const { return off_; }
  const char* debug();
  Motion motion;

  void show(int i);                                   // direkt zu einer Kachel (Test, ohne Überblendung)
  void setHome(int i) { home_ = i; }                  // Dashboard: Startseite, dorthin führt das ✕
  void setSettingsHold(uint32_t ms) { holdMs_ = ms; } // so lange drücken für die Einstellungen (Kindersicherung)
  // Ruhe: nach dimMs dunkler, nach offMs schwarz. startOff: nach dem Einschalten schwarz bis zum ersten Tippen.
  // wakeOnMotion: Klopfen/Schütteln weckt auch (sonst nur Berührung und BOOT)
  void setPower(uint32_t dimMs, uint32_t offMs, bool startOff, bool wakeOnMotion) { dimMs_ = dimMs; offMs_ = offMs; startOff_ = startOff; wakeOnMotion_ = wakeOnMotion; }

  int current() const { return cur_; }
  bool closeShown() const { return cur_ != home_ && tiles_[cur_]->showsClose(); }
  static constexpr float CLOSE_X = W - 44, CLOSE_Y = 44, CLOSE_R = 19;   // ✕-Knopf

 private:
  void setCurrent(int i);
  void go(int target);        // mit Überblendung wechseln (Index, GOTO_BACK, GOTO_HOME)
  void wake();
  void openSettings();
  void settingsTap(float x, float y);
  void drawSettings(Scene& s);
  int settingsItems() const;
  void applySound();

  Synth& synth_;
  Store& store_;
  std::vector<Tile*> tiles_;
  std::vector<int> back_;     // wer hat die Kachel geöffnet (für GOTO_BACK)
  int cur_ = 0, home_ = 0;

  // Überblendung beim Öffnen/Schließen
  int transTo_ = -1;
  bool transReturning_ = false;
  float trans_ = 0;           // 0 = ruhig, steigt beim Ausblenden bis 1, dann wird gewechselt und eingeblendet

  // Touch
  enum Mode { NONE, PENDING, SWIPE, DONE } mode_ = NONE;
  bool swipeOk_ = false, swipeH_ = false;   // darf gewischt werden, waagerecht (schließen) oder senkrecht (blättern)
  float dragX_ = 0, dragY_ = 0;             // Bild folgt dem Finger
  float slideX_ = 0, slideY_ = 0;           // Richtung der laufenden Überblendung nach einem Wisch
  int swipeTarget(float d) const;           // Ziel eines senkrechten Wischs (-1 = keins)
  bool touching_ = false;
  float sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0, touchPeak_ = 0;
  uint32_t sMs_ = 0, releaseMs_ = 0, shakeMs_ = 0;
  bool bootWas_ = false;

  // Anzeige
  uint32_t ms_ = 0, lastAct_ = 0;
  float dt_ = 0;
  bool settings_ = false;
  float settingsA_ = 0;
  int sound_ = 0;   // 0 aus, 1 leise, 2 an
  int bright_ = 200;
  bool off_ = false, dim_ = false;
  uint32_t holdMs_ = 700, dimMs_ = 120000, offMs_ = 600000;
  bool startOff_ = false, wakeOnMotion_ = true;
  float orbX_ = 0, orbY_ = 0;
  char dbg_[160];
};

}  // namespace dk
