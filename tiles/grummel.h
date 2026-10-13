// Kachel „Dampf ablassen“: Klopfen oder Schütteln schleudert dunkle Partikel in Grummel, er frisst sie,
// wird satt, rülpst und verwandelt die Wut in helle Blasen. Danach atmet er dreimal mit dir.
// Läuft komplett auf dem Gerät. Aussehen und Texte stecken im Skin, damit die Kinderfigur später dieselbe Logik nutzt.
#pragma once
#include "input.h"
#include "synth.h"
#include "tile.h"

namespace dk {

struct GrummelSkin {
  const char* title;
  Col top, bottom;            // Körper satt-zufrieden
  Col fullTop, fullBottom;    // Körper voller Wut
  Col cheek, aura, ring;
  Col darkCore, darkRim, darkEdge;   // die Wut-Partikel
  const char* hint;           // Hinweis im Bereitschaftszustand
  const char* thanks;
  const char* intro;          // vor dem Atmen
  const char* inhaleHint;     // zweite Zeile beim Einatmen (nullptr = keine)
  const char* exhaleHint;
  int breaths;                // Atemzüge in der Ruhephase
  float inhale, exhale;       // Sekunden
  bool kid;                   // große Schrift, Sterne statt Statistik, danach zurück zur Startkachel
};

extern const GrummelSkin OFFICE_SKIN, KID_SKIN;

class Grummel : public Tile {
 public:
  Grummel(Synth& synth, Store& store, uint32_t seed, const GrummelSkin& skin = OFFICE_SKIN);
  const char* title() const override { return skin_.title; }
  void enter(uint32_t ms) override;
  void tick(const TileInput& in) override;
  void draw(Scene& s) override;
  bool keepAwake() const override;
  bool wantsMotion() const override { return true; }
  float motionThreshold() const override;
  int settingsCount() const override { return 3; }
  void settingText(int i, char* buf, int n) const override;
  bool settingTap(int i) override;

  // für den Simulator
  enum Phase { DOZE, READY, CHARGE, FULL, TRANSFORM, BREATHE, THANKS, CALIB };
  Phase phase() const { return phase_; }
  float energy() const { return energy_; }

 private:
  struct Dark { float x, y, vx, vy, r, val, ph; bool homing; };
  struct Light { float x, y, vx, vy, r, life, max, ph; uint8_t kind; Col c; };

  void setPhase(Phase p);
  void feed(float strength, float dirX, float dirY, bool fromShake);
  void feedTap(float x, float y, float strength);   // Kinder: Tippen aufs Display wirft Wutfunken von dort hinein
  void spawnDark(float ang, float val, bool homing);
  void spawnLight();
  void countSession(int64_t epoch);
  void statsText(char* buf, int n) const;
  float rnd();   // 0..1
  void drawBody(Scene& s);
  void tickCalib(const TileInput& in);
  void drawCalib(Scene& s);

  // Klänge
  void sMunch();
  void sBurp();
  void sRise();
  void sHuh();

  Synth& synth_;
  Store& store_;
  const GrummelSkin& skin_;
  uint32_t seed_;

  Phase phase_ = DOZE;
  float t_ = 0;           // Zeit in der aktuellen Phase
  float clock_ = 0;       // Gesamtzeit
  int64_t epoch_ = 0;

  // Körper
  float energy_ = 0, eVis_ = 0;
  float R_ = 62;
  float sq_ = 0, sqv_ = 0;             // Quetschen (Feder)
  float chew_ = 0, mouthOpen_ = 0, eyeOpen_ = 1, happy_ = 0, peace_ = 0, puff_ = 0, lean_ = 0;
  float lookX_ = 0, lookY_ = 0, lookTX_ = 0, lookTY_ = 0;
  float blinkT_ = 2, hopT_ = 6, lookT_ = 1;
  float driftX_ = 0, driftY_ = 0;
  float jitter_ = 0;
  float startle_ = 0, pokeT_ = 0, wander_ = 0;

  // Laden
  float lastFeed_ = 0, lastKnock_ = -10, munchCd_ = 0;
  float hintT_ = 0;
  float burpT_ = -1;
  float breath_ = 0;
  int breathN_ = 0;
  float zT_ = 0;
  float shakeAcc_ = 0, lightAcc_ = 0;

  Dark dark_[96];
  int nDark_ = 0;
  Light light_[96];
  int nLight_ = 0;

  // Einmessen: erst Tippen (Grundunruhe), dann sechs Schläge, Schwelle dazwischen
  int calStep_ = 0;
  float calPeaks_[320];
  int calN_ = 0;
  float calTyp_ = 0, calKnock_[6], calLast_ = -1;
  int calKn_ = 0;
  bool calOk_ = false;
  float calThr_ = 0;

  // Einstellungen
  int sens_ = 1;          // 0 fein, 1 mittel, 2 fest, 3 eingemessen
  float thrCustom_ = 0;   // eingemessene Schwelle in g (0 = keine)
  bool meter_ = false;    // Messwerte zeigen
  float peaks_[64] = {};
  int peakI_ = 0;
  float thr_ = 0.1f;
  int allN_ = 0, wkId_ = -1, wkN_ = 0;   // Statistik (NVS)
  int stars_ = 0;
};

}  // namespace dk
