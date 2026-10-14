// Abgleich zwischen Launcher und Eltern-Portal, einmal pro Frame (Gerät und Simulator):
// Sperre in beide Richtungen, Zustand des Geräts fürs Portal.
#pragma once
#include <cstdio>

#include "app.h"
#include "radio_player.h"

namespace dk {

struct PortalSync {
  int mode = -2, from = -1, to = -1;   // zuletzt vom Portal übernommen
  long apps = -2;
  uint32_t nextState = 0;

  void frame(App& app, RadioPlayer& radio, const char* tile, int stars, uint32_t ms) {
    char b[96];
    if (app.takeLockChange()) {   // am Gerät umgestellt: dem Portal melden
      snprintf(b, sizeof b, "/api/sperre?modus=%d", app.lockMode());
      radio.queueAction(b);
    }
    // nur übernehmen, wenn das Portal etwas Neues sagt (sonst würde eine Änderung am Gerät gleich überschrieben)
    if (radio.lockMode >= 0 && (radio.lockMode != mode || radio.lockFrom != from || radio.lockTo != to)) {
      mode = radio.lockMode; from = radio.lockFrom; to = radio.lockTo;
      app.setLock(mode, from, to);
    }
    if (app.takeAppsChange()) {
      snprintf(b, sizeof b, "/api/apps?aus=%lu", (unsigned long)app.disabled());
      radio.queueAction(b);
    }
    if (radio.appsOff >= 0 && radio.appsOff != apps) {
      apps = radio.appsOff;
      app.setDisabled(uint32_t(apps));
    }
    if (ms >= nextState) {
      nextState = ms + 1000;
      char t[48];
      encode(tile, t, sizeof t);
      snprintf(b, sizeof b, "kachel=%s&sterne=%d&aus=%lu", t, stars, (unsigned long)app.disabled());
      radio.setDeviceState(b);
    }
  }

  static void encode(const char* s, char* out, int n) {   // für die Abfrage (Umlaute, Leerzeichen)
    static const char* hexd = "0123456789ABCDEF";
    int k = 0;
    for (; *s && k < n - 4; s++) {
      const unsigned char c = static_cast<unsigned char>(*s);
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') out[k++] = char(c);
      else { out[k++] = '%'; out[k++] = hexd[c >> 4]; out[k++] = hexd[c & 15]; }
    }
    out[k] = 0;
  }
};

}  // namespace dk
