// Klopfen und Schütteln aus den Beschleunigungswerten erkennen.
// Klopfen: kurzer Ausschlag über der Schwelle, danach Sperrzeit, damit das Nachschwingen des Tischs nicht doppelt zählt.
// Schütteln: Bewegungsenergie über ~0,4 g für 150 ms, endet nach 300 ms Ruhe; währenddessen zählen keine Einzelschläge.
#pragma once
#include <cstdint>

#include "input.h"

namespace dk {

struct Knock {
  float strength;   // 0..1 (Schwelle = 0, vierfache Schwelle = 1)
  float peak;       // Ausschlag in g
  float dirX, dirY; // grobe Richtung in der Bildschirmebene (0, wenn unklar)
  uint32_t ms;
};

class Motion {
 public:
  float threshold = 0.10f;   // g, siehe Empfindlichkeit

  // touchBusy: Finger liegt auf oder ging gerade erst weg (Tippen auf das Display schüttelt das Gerät auch)
  void feed(const AccelSample* s, int n, bool touchBusy);

  Knock knocks[8];
  int nKnocks = 0;
  bool shaking = false;
  float shake = 0;        // 0..1 Stärke des Schüttelns
  float framePeak = 0;    // größter Ausschlag dieses Frames in g (Kalibrierung)
  float noise = 0;        // Grundunruhe in g (gleitend)
  uint32_t totalKnocks = 0;
  float tiltX = 0, tiltY = 0, tiltZ = 1;   // Beschleunigung schnell geglättet (~50 ms), für Neige-Spiele

 private:
  bool init_ = false;
  float gx_ = 0, gy_ = 0, gz_ = 1;   // Schwerkraft (Tiefpass)
  uint32_t lastMs_ = 0, lastKnock_ = 0, burstStart_ = 0, shakeSince_ = 0, quietSince_ = 0;
  bool burst_ = false;
  float burstPeak_ = 0, bx_ = 0, by_ = 0;
  float energy_ = 0;
};

}  // namespace dk
