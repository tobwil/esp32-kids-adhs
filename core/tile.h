// Schnittstelle einer Kachel. Der Launcher (App) erkennt die Gesten, die Kachel bekommt nur fertige Ereignisse.
#pragma once
#include <cstdint>

#include "motion.h"
#include "render.h"

namespace dk {

struct TileInput {
  uint32_t ms = 0;
  float dt = 0;
  int64_t epoch = 0;              // Unix-Zeit, 0 = unbekannt
  bool tap = false;               // kurzer Tipp auf das Display
  float tapX = 0, tapY = 0;
  float tapPeak = 0;              // stärkster Ausschlag der IMU während der Berührung (g): wie kräftig getippt wurde
  bool finger = false;            // Finger liegt gerade auf
  float fingerX = 0, fingerY = 0;
  const Knock* knocks = nullptr;  // Klopfen auf den Tisch (nur wenn wantsMotion())
  int nKnocks = 0;
  bool shaking = false;
  float shake = 0;
  float framePeak = 0;            // für die Messwert-Anzeige
  float tiltX = 0, tiltY = 0, tiltZ = 1;   // Neigung (g, Bildschirmachsen: x rechts, y oben, z aus dem Bildschirm)
  float voice = 0;                // Lautstärke am Mikrofon 0..1 (nur der Pegel, nichts wird aufgenommen)
  bool locked = false;            // Sperre aktiv (z. B. Schlafenszeit: nur Radio)
  uint32_t disabled = 0;          // von den Eltern abgeschaltete Kacheln (Bit je Index)
  float threshold = 0;
};

class Tile {
 public:
  virtual ~Tile() = default;
  virtual const char* title() const = 0;
  virtual void enter(uint32_t ms) {}
  virtual void leave() {}
  // per Navigation hierher gekommen (nach enter). returning: zurück aus einer App, die diese Kachel geöffnet hatte
  virtual void arrive(bool returning) {}
  // Wunsch, zu einer anderen Kachel zu wechseln (Index oder GOTO_BACK/GOTO_HOME, -1 = keiner); die App holt ihn nach jedem tick ab
  static constexpr int GOTO_BACK = -2, GOTO_HOME = -3;
  int takeGoto() { const int g = goto_; goto_ = -1; return g; }
  virtual void tick(const TileInput& in) = 0;
  virtual void draw(Scene& s) = 0;
  virtual bool keepAwake() const { return false; }   // Display nicht dimmen (z. B. Meeting läuft, Grummel frisst)
  virtual bool wantsMotion() const { return false; }
  // Kachel nutzt den Finger selbst (Joystick, Streicheln): langes Halten öffnet keine Einstellungen
  virtual bool claimsDrag() const { return false; }
  // Finger, der an (x, y) aufsetzt, gehört der Kachel: kein Wischen zwischen den Apps (außer vom Rand aus)
  virtual bool claimsDragAt(float x, float y) const { return claimsDrag(); }
  virtual float motionThreshold() const { return 0.10f; }

  // Einstellungen der Kachel (erscheinen nach langem Druck unter den allgemeinen)
  virtual int settingsCount() const { return 0; }
  virtual void settingText(int i, char* buf, int n) const {}
  virtual bool settingTap(int i) { return false; }   // true: Einstellungen danach schließen

 protected:
  void requestGoto(int tile) { goto_ = tile; }

 private:
  int goto_ = -1;
};

}  // namespace dk
