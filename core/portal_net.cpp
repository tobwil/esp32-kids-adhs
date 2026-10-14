#include "portal_net.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace dk {

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---------------- HTTP-Antwort lesen (auch Chunked) ----------------

struct Body {
  NetConn* c = nullptr;
  bool chunked = false;
  long left = -1;        // Chunked: Bytes im aktuellen Stück (-1 = Kopf des ersten Stücks lesen)
  bool done = false;
  uint8_t pre[1024];     // nach den Kopfzeilen schon gelesene Bytes
  int preN = 0, preI = 0;
};

static int rawRead(Body& b, uint8_t* buf, int n, int timeoutMs) {
  if (b.preI < b.preN) {
    const int k = b.preN - b.preI < n ? b.preN - b.preI : n;
    std::memcpy(buf, b.pre + b.preI, k);
    b.preI += k;
    return k;
  }
  return netRead(b.c, buf, n, timeoutMs);
}

static bool rawByte(Body& b, uint8_t& v, int timeoutMs) {
  for (int tries = 0; tries < 10; tries++) {
    const int r = rawRead(b, &v, 1, timeoutMs);
    if (r == 1) return true;
    if (r != -2) return false;
  }
  return false;
}

// >0 Bytes, 0 = Ende, -1 = Fehler, -2 = Zeitlimit (Strom pausiert)
static int bodyRead(Body& b, uint8_t* buf, int n, int timeoutMs) {
  if (b.done) return 0;
  if (!b.chunked) return rawRead(b, buf, n, timeoutMs);
  if (b.left <= 0) {
    uint8_t c = 0;
    if (b.left == 0) { rawByte(b, c, timeoutMs); rawByte(b, c, timeoutMs); }   // CRLF nach dem Stück
    char line[20];
    int k = 0;
    while (k < int(sizeof line) - 1) {
      if (!rawByte(b, c, timeoutMs)) return -1;
      if (c == '\n') break;
      if (c != '\r') line[k++] = char(c);
    }
    line[k] = 0;
    b.left = std::strtol(line, nullptr, 16);
    if (b.left <= 0) { b.done = true; return 0; }
  }
  const int r = rawRead(b, buf, n < b.left ? n : int(b.left), timeoutMs);
  if (r > 0) b.left -= r;
  return r;
}

// Anfrage senden und Kopfzeilen lesen; false bei Fehler oder Status ≠ 200
static bool request(const Endpoint& ep, const char* method, const char* path, const uint8_t* data, int len, Body& b) {
  b.c = nullptr; b.chunked = false; b.left = -1; b.done = false; b.preN = b.preI = 0;
  if (!ep.valid()) return false;
  b.c = netOpen(ep, 4000);
  if (!b.c) return false;
  char req[400];
  int n = snprintf(req, sizeof req, "%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n", method, path, ep.host);
  if (ep.key[0]) n += snprintf(req + n, sizeof req - n, "Authorization: Bearer %s\r\n", ep.key);
  if (data) n += snprintf(req + n, sizeof req - n, "Content-Type: application/octet-stream\r\nContent-Length: %d\r\n", len);
  n += snprintf(req + n, sizeof req - n, "\r\n");
  bool ok = netWrite(b.c, req, n) == n;
  for (int off = 0; ok && data && off < len;) {
    const int w = netWrite(b.c, data + off, len - off < 4096 ? len - off : 4096);
    if (w <= 0) ok = false;
    else off += w;
  }
  // Kopfzeilen (nach einem POST darf das Portal erst rechnen)
  char head[1024];
  int have = 0;
  char* end = nullptr;
  for (int tries = 0; ok && !end && tries < (data ? 20 : 8); tries++) {
    const int r = netRead(b.c, head + have, int(sizeof head) - 1 - have, 1000);
    if (r == -2) continue;
    if (r <= 0) break;
    have += r;
    head[have] = 0;
    end = std::strstr(head, "\r\n\r\n");
    if (have >= int(sizeof head) - 1) break;
  }
  if (!ok || !end || have < 12 || std::strncmp(head + 9, "200", 3) != 0) { netClose(b.c); b.c = nullptr; return false; }
  b.preN = int(head + have - (end + 4));
  std::memcpy(b.pre, end + 4, b.preN);
  *end = 0;
  for (char* p = head; *p; p++) *p = char(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);   // Kopfzeilen ohne Groß/klein
  b.chunked = std::strstr(head, "transfer-encoding: chunked") != nullptr;
  return true;
}

static bool fetch(const Endpoint& ep, const char* path, char* out, int cap) {
  Body b;
  if (!request(ep, "GET", path, nullptr, 0, b)) return false;
  int n = 0;
  for (;;) {
    if (n >= cap - 1) break;
    const int r = bodyRead(b, reinterpret_cast<uint8_t*>(out + n), cap - 1 - n, 3000);
    if (r <= 0) break;
    n += r;
  }
  out[n] = 0;
  netClose(b.c);
  return true;
}

bool portalDownload(const Endpoint& ep, const char* path, bool (*sink)(void*, const uint8_t*, int), void* ctx) {
  Body b;
  if (!request(ep, "GET", path, nullptr, 0, b)) return false;
  uint8_t buf[1024];
  bool ok = true;
  for (;;) {
    const int r = bodyRead(b, buf, sizeof buf, 5000);
    if (r == 0) break;
    if (r < 0) { ok = false; break; }   // Zeitlimit oder Fehler: unvollständig
    if (!sink(ctx, buf, r)) { ok = false; break; }
  }
  netClose(b.c);
  return ok;
}

bool portalPost(const Endpoint& ep, const char* path, const uint8_t* data, int len) {
  Body b;
  const bool ok = request(ep, "POST", path, data, len, b);
  if (b.c) netClose(b.c);
  return ok;
}

bool portalPoll(RadioPlayer& p, Messages* m, const Endpoint& ep) {
  char path[256], body[4096];
  bool ok = true;
  while (ok && p.takeAction(path, sizeof path)) ok = fetch(ep, path, body, sizeof body);
  while (ok && m && m->takeAction(path, sizeof path)) ok = fetch(ep, path, body, sizeof body);
  const int16_t* rec;
  int samples;
  if (ok && m && m->takeUpload(path, sizeof path, &rec, &samples))
    m->uploadDone(portalPost(ep, path, reinterpret_cast<const uint8_t*>(rec), samples * 2));
  p.stateQuery(path, sizeof path);
  ok = ok && fetch(ep, path, body, sizeof body);
  if (ok) p.parse(body);
  p.setOnline(ok);
  return ok;
}

void portalStreamLoop(RadioPlayer& p, Endpoint (*endpoint)(), LocalFiles* local) {
  static Body b;   // groß (Vorlesepuffer), nicht auf den Stapel
  int16_t pcm[513];
  for (;;) {
    char path[64];
    const uint32_t gen = p.wantedStream(path, sizeof path);
    if (!path[0]) { sleepMs(50); continue; }
    if (local && local->open(path)) {   // von der SD-Karte, auch ohne Netz
      uint8_t buf[1024];
      bool ended = false;
      while (p.wantedStream(path, sizeof path) == gen) {
        if (p.ring.space() < 512) { sleepMs(20); continue; }
        const int r = local->read(buf, sizeof buf) & ~1;
        if (r <= 0) { ended = true; break; }
        p.ring.write(reinterpret_cast<int16_t*>(buf), r / 2);
      }
      local->close();
      if (ended) {
        while (p.ring.fill() > 0 && p.wantedStream(path, sizeof path) == gen) sleepMs(50);
        p.streamEnded(gen);
      }
      continue;
    }
    if (!request(endpoint(), "GET", path, nullptr, 0, b)) { sleepMs(1000); continue; }
    // Bytes zu Samples; ein ungerades Byte wartet auf das nächste Stück
    uint8_t carry = 0;
    bool hasCarry = false, ended = false;
    uint8_t buf[1024];
    while (p.wantedStream(path, sizeof path) == gen) {
      if (p.ring.space() < 1024) { sleepMs(20); continue; }   // Puffer voll: das Portal wartet (TCP bremst)
      const int r = bodyRead(b, buf, sizeof buf, 1000);
      if (r == 0) { ended = true; break; }
      if (r == -2) continue;   // Zeitlimit, z. B. Spotify pausiert
      if (r < 0) break;
      int k = 0, m = 0;
      if (hasCarry) { pcm[m++] = int16_t(carry | (buf[k++] << 8)); hasCarry = false; }
      while (k + 1 < r) { pcm[m++] = int16_t(buf[k] | (buf[k + 1] << 8)); k += 2; }
      if (k < r) { carry = buf[k]; hasCarry = true; }
      p.ring.write(pcm, m);
    }
    netClose(b.c);
    if (ended) {
      // Aufnahme zu Ende: abspielen lassen, was noch im Puffer ist, dann aufhören
      while (p.ring.fill() > 0 && p.wantedStream(path, sizeof path) == gen) sleepMs(50);
      p.streamEnded(gen);
    }
  }
}

}  // namespace dk
