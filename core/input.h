// Eingaben der Plattform an die App, einmal pro Frame (30 Hz).
#pragma once
#include <cstdint>

namespace dk {

struct TouchSample { bool down; int16_t x, y; uint32_t ms; };   // Display-Pixel (368×448)
struct AccelSample { float x, y, z; uint32_t ms; };             // g, Bildschirmachsen: x rechts, y oben, z aus dem Bildschirm

struct Input {
  TouchSample touch[32];   // alle Touch-Abtastwerte seit dem letzten Frame
  int nTouch = 0;
  AccelSample acc[64];     // alle Beschleunigungswerte seit dem letzten Frame (Gerät: ~500 Hz)
  int nAcc = 0;
  bool boot = false;       // BOOT-Taste gedrückt
  uint32_t ms = 0;
  int64_t epoch = 0;       // Unix-Zeit in Sekunden (Ortszeit), 0 = unbekannt
};

struct Store {   // dauerhafte Werte (NVS auf dem Gerät, Datei im Simulator)
  virtual int get(const char* key, int def) = 0;
  virtual void set(const char* key, int v) = 0;
  virtual ~Store() = default;
};

}  // namespace dk
