// Verbindungen fürs Portal auf dem Gerät: WiFiClient (Mac-Portal im Heimnetz) oder WiFiClientSecure mit
// geprüftem Zertifikat (Cloud-Portal). Siehe core/portal_net.h.
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "portal_net.h"
#include "root_cas.h"

struct dk::NetConn {
  WiFiClient* c;
};

dk::NetConn* dk::netOpen(const Endpoint& ep, int timeoutMs) {
  WiFiClient* c;
  if (ep.tls) {
    auto* s = new WiFiClientSecure();
    s->setCACert(ROOT_CAS);
    s->setHandshakeTimeout(timeoutMs / 1000 + 4);
    c = s;
  } else {
    c = new WiFiClient();
  }
  if (!c->connect(ep.host, ep.port, timeoutMs)) { delete c; return nullptr; }
  c->setNoDelay(true);
  return new NetConn{c};
}

int dk::netWrite(NetConn* n, const void* data, int len) {
  const size_t w = n->c->write(static_cast<const uint8_t*>(data), size_t(len));
  return w == 0 && len > 0 ? -1 : int(w);
}

int dk::netRead(NetConn* n, void* buf, int len, int timeoutMs) {
  const uint32_t t0 = millis();
  for (;;) {
    const int a = n->c->available();
    if (a > 0) {
      const int r = n->c->read(static_cast<uint8_t*>(buf), size_t(a < len ? a : len));
      return r > 0 ? r : -1;
    }
    if (!n->c->connected()) return 0;
    if (int(millis() - t0) >= timeoutMs) return -2;
    delay(3);
  }
}

void dk::netClose(NetConn* n) {
  if (!n) return;
  n->c->stop();
  delete n->c;
  delete n;
}
